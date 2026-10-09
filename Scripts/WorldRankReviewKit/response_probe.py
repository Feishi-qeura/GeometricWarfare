"""One bounded probe of the failed version-setting job, without queue mutation."""
import argparse
from contextlib import closing
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import sys
import urllib.error
import urllib.request

FIELDS = frozenset(('err_no', 'errcode', 'error_code', 'errno', 'error', 'code', 'status_code', 'status', 'BaseResp', 'StatusCode', 'StatusMessage', 'err', 'err_code', 'extra', 'data', 'access_token', 'expires_in', 'err_msg', 'err_tips', 'message', 'msg'))
ERROR_FIELDS = frozenset(('err_no', 'errcode', 'error_code', 'errno', 'error', 'code', 'status_code', 'err_code', 'StatusCode'))


def summarize_response(value, depth=0):
    result = {'type': 'null' if value is None else type(value).__name__}
    if not isinstance(value, dict) or depth >= 3:
        return result
    result['fields'] = {}
    result['unknown_field_count'] = sum(key not in FIELDS for key in value)
    for key in sorted(FIELDS.intersection(value)):
        entry = value[key]
        child = summarize_response(entry, depth + 1)
        if key in ERROR_FIELDS:
            if type(entry) is int and -1 <= entry <= 999999999:
                child['numeric_code'] = entry
            elif isinstance(entry, str) and re.fullmatch(r'-?[0-9]{1,9}', entry):
                child['numeric_code'] = int(entry)
        if key == 'expires_in' and type(entry) is int:
            child['valid_positive_duration'] = 0 < entry <= 86400
        result['fields'][key] = child
    return result


def describe_http_response(status, content_type, body):
    media = content_type.split(';', 1)[0].strip().lower()
    record = {'http_status': status, 'content_type': media if media in ('application/json', 'text/json', 'text/html', 'text/plain', 'application/octet-stream') else 'other', 'body_bytes': len(body), 'json_valid': False}
    try:
        value = json.loads(body)
    except (ValueError, UnicodeError):
        return record, None
    record['json_valid'] = True
    record['response_shape'] = summarize_response(value)
    return record, value


def run_probe(backend_directory, environment_file=None, expected_job_id=1):
    root = Path(backend_directory).resolve()
    env_file = Path(environment_file).resolve() if environment_file else root / '.env'
    for line in env_file.read_text(encoding='utf-8-sig').splitlines():
        if not line.strip() or line.lstrip().startswith('#'): continue
        key, separator, value = line.partition('=')
        if not separator or not re.fullmatch(r'BACKEND_[A-Z0-9_]+', key):
            raise ValueError('invalid_environment_file')
        os.environ[key] = value
    sys.path.insert(0, str(root))
    from live_backend.service import Settings
    from live_backend.upstream import DouyinAPI, HTTPSJSONTransport, TOKEN_URL, WORLD_BASE, UpstreamError, validate_world_body
    settings = Settings.from_env()
    if not settings.app_id or not settings.app_secret:
        raise ValueError('server_credentials_not_configured')
    # This review probe is restricted to the user's unreleased test environment.
    if settings.online:
        raise ValueError('review_probe_requires_test_environment')
    database = Path(settings.database_path)
    if not database.is_absolute(): database = root / database
    database = database.resolve()
    if not database.is_file(): raise ValueError('configured_database_not_found')
    with closing(sqlite3.connect(database.as_uri() + '?mode=ro', uri=True, timeout=5)) as db:
        db.row_factory = sqlite3.Row
        db.execute('PRAGMA query_only=ON')
        head = db.execute("SELECT id,operation,state,body FROM outbox WHERE app_id=? AND online=? AND state!='done' ORDER BY id LIMIT 1", (settings.app_id, 0)).fetchone()
    if not head or head['id'] != expected_job_id or head['operation'] != 'set_valid_version' or head['state'] != 'failed':
        raise ValueError('expected_failed_version_job_required')
    body = json.loads(head['body'])
    validate_world_body('set_valid_version', body)
    if body['app_id'] != settings.app_id or body['is_online_version'] is not False:
        raise ValueError('world_identity_mismatch')

    class ShapeTransport(HTTPSJSONTransport):
        def __init__(self):
            super().__init__()
            self.records = []
        def post(self, url, headers, request_body):
            if url not in (TOKEN_URL, WORLD_BASE + 'set_valid_version'):
                raise UpstreamError('probe_endpoint_rejected', transient=False)
            stage = 'application_token' if url == TOKEN_URL else 'set_valid_version'
            request = urllib.request.Request(url, data=json.dumps(request_body, ensure_ascii=False, allow_nan=False, separators=(',', ':')).encode(), headers=headers, method='POST')
            try:
                with self.opener.open(request, timeout=15) as response:
                    raw = response.read(1024 * 1024 + 1)
                    status = response.status
                    content_type = response.headers.get('Content-Type', '')
            except urllib.error.HTTPError as error:
                raw = error.read(1024 * 1024 + 1)
                record, _ = describe_http_response(error.code, error.headers.get('Content-Type', ''), raw) if len(raw) <= 1024 * 1024 else ({'http_status': error.code, 'response_limit_exceeded': True}, None)
                self.records.append({'stage': stage, **record})
                raise UpstreamError('upstream_http_' + str(error.code), transient=False) from None
            except UpstreamError:
                self.records.append({'stage': stage, 'transport_error': 'redirect_or_transport_rejected'})
                raise
            except Exception:
                self.records.append({'stage': stage, 'transport_error': 'network_or_tls_unavailable'})
                raise UpstreamError('upstream_unavailable') from None
            if len(raw) > 1024 * 1024:
                self.records.append({'stage': stage, 'response_limit_exceeded': True})
                raise UpstreamError('upstream_response_limit', transient=False)
            record, parsed = describe_http_response(status, content_type, raw)
            self.records.append({'stage': stage, **record})
            if status != 200 or not record['json_valid'] or not isinstance(parsed, dict):
                raise UpstreamError('upstream_response_shape', transient=False)
            return parsed

    transport = ShapeTransport()
    api = DouyinAPI(settings.app_id, settings.app_secret, transport=transport)
    result = {'job_id': head['id'], 'operation': 'set_valid_version', 'online_version': False, 'queue_unchanged': True, 'version_digest': hashlib.sha256(body['world_rank_version'].encode()).hexdigest()[:12]}
    try:
        # Uses exactly the installed backend's validation. A malformed response
        # is never promoted to success by this diagnostic wrapper.
        api.publish('set_valid_version', body)
    except UpstreamError as error:
        result['installed_backend_validation'] = 'rejected'
        result['error'] = error.code if re.fullmatch(r'[a-z_0-9]{1,80}', error.code) else 'redacted_error'
    else:
        result['installed_backend_validation'] = 'accepted'
    result['requests'] = transport.records
    result['installed_upstream_sha256'] = hashlib.sha256((root / 'live_backend/upstream.py').read_bytes()).hexdigest()
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--backend-directory', required=True)
    parser.add_argument('--environment-file')
    parser.add_argument('--expected-job-id', type=int, default=1)
    arguments = parser.parse_args()
    try:
        report = run_probe(arguments.backend_directory, arguments.environment_file, arguments.expected_job_id)
        print(json.dumps(report, ensure_ascii=True, indent=2))
        return 0
    except Exception as error:
        # No exception message or traceback; configuration/transport exceptions
        # may embed raw values in third-party runtimes.
        safe_codes = {'invalid_environment_file', 'server_credentials_not_configured', 'review_probe_requires_test_environment', 'configured_database_not_found', 'expected_failed_version_job_required', 'world_identity_mismatch', 'invalid_boolean_configuration', 'invalid_completion_unit', 'invalid_session_ttl'}
        code = str(error) if isinstance(error, ValueError) and str(error) in safe_codes else 'probe_precondition_or_runtime_failed'
        print(json.dumps({'probe_error': code, 'queue_unchanged': True}))
        return 1


if __name__ == '__main__': raise SystemExit(main())
