"""Read-only server diagnosis. Never emit credentials, users or queued bodies."""
import argparse
from contextlib import closing
import json
from pathlib import Path
import re
import sqlite3
import urllib.request


class DiagnosticError(Exception):
    pass


class RejectRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        raise DiagnosticError('loopback_redirect_rejected')


def inspect_server(backend_directory, *, environment_file=None, runtime_health=None):
    root = Path(backend_directory).resolve()
    env_file = Path(environment_file).resolve() if environment_file else root / '.env'
    values = {}
    try:
        for line in env_file.read_text(encoding='utf-8-sig').splitlines():
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            key, separator, value = line.partition('=')
            if not separator or not re.fullmatch(r'BACKEND_[A-Z0-9_]+', key):
                raise DiagnosticError('invalid_environment_file')
            values[key] = value
    except OSError:
        raise DiagnosticError('environment_file_not_readable') from None

    def boolean(key):
        value = values.get(key, 'false').lower()
        if value not in ('true', 'false'):
            raise DiagnosticError('invalid_boolean_configuration')
        return value == 'true'

    app_id = values.get('BACKEND_APP_ID', '')
    online, enabled = boolean('BACKEND_ONLINE_VERSION'), boolean('BACKEND_ENABLE_UPLOADS')
    unit = values.get('BACKEND_COMPLETE_TIME_UNIT', '')
    if unit not in ('', 'seconds', 'milliseconds'):
        raise DiagnosticError('invalid_completion_unit')
    database = Path(values.get('BACKEND_DATABASE_PATH', 'data/live-backend.sqlite3'))
    if not database.is_absolute():
        database = root / database
    database = database.resolve()
    if not database.is_file():
        raise DiagnosticError('configured_database_not_found')
    operations = ('set_valid_version', 'upload_user_result', 'upload_rank_list', 'complete_upload_user_result')
    scope = (app_id, int(online))
    counts = {operation: {} for operation in operations}
    outbox = {}
    # mode=ro forbids creating the DB or changing schema, totals, jobs or leases.
    try:
        with closing(sqlite3.connect(database.as_uri() + '?mode=ro', uri=True, timeout=5)) as db:
            db.row_factory = sqlite3.Row
            db.execute('PRAGMA query_only=ON')
            db.execute('BEGIN')
            for row in db.execute('SELECT operation,state,count(*) count FROM outbox WHERE app_id=? AND online=? GROUP BY operation,state', scope):
                if row['operation'] in operations and row['state'] in ('pending', 'leased', 'failed', 'done'):
                    counts[row['operation']][row['state']] = row['count']
                    outbox[row['state']] = outbox.get(row['state'], 0) + row['count']
            head = db.execute("SELECT id,operation,state,attempts,error FROM outbox WHERE app_id=? AND online=? AND state!='done' ORDER BY id LIMIT 1", scope).fetchone()
            rounds = db.execute('SELECT count(*) FROM rounds WHERE app_id=? AND online=?', scope).fetchone()[0]
            users = db.execute('SELECT count(*) FROM totals WHERE app_id=? AND online=?', scope).fetchone()[0]
    except sqlite3.Error:
        raise DiagnosticError('database_read_failed') from None
    head = dict(head) if head else None
    if head:
        if head['operation'] not in operations:
            head['operation'] = 'invalid_operation'
        if head['error'] is not None and not re.fullmatch(r'[a-z][a-z0-9_]{0,79}', head['error']):
            head['error'] = 'redacted_nonstandard_error'

    runtime = {}
    if isinstance(runtime_health, dict):
        for key in ('status', 'uploads_enabled', 'complete_time_unit', 'publication_state'):
            value = runtime_health.get(key)
            if type(value) is bool or isinstance(value, str) and re.fullmatch(r'[a-z_]{1,60}', value):
                runtime[key] = value
    blockers = []
    if not app_id or not values.get('BACKEND_APP_SECRET'):
        blockers.append('server_credentials_not_configured')
    if not enabled:
        blockers.append('uploads_disabled')
    if runtime.get('uploads_enabled') is not None and runtime['uploads_enabled'] != enabled:
        blockers.append('runtime_restart_required')
    if head and head['state'] == 'failed':
        blockers.append('head_job_failed')
    if head and head['operation'] == 'complete_upload_user_result' and not unit:
        blockers.append('completion_time_unit_required')
    return {
        'configuration': {'credentials_configured': bool(app_id and values.get('BACKEND_APP_SECRET')), 'online_version': online, 'uploads_enabled': enabled, 'complete_time_unit': unit or 'pending_verification'},
        'runtime_health': runtime,
        'outbox': outbox,
        'operations': counts,
        'review_apis': {operation: {'confirmed_jobs': counts[operation].get('done', 0), 'pending_jobs': counts[operation].get('pending', 0), 'failed_jobs': counts[operation].get('failed', 0)} for operation in ('upload_user_result', 'upload_rank_list')},
        'head_job': head,
        'archived_rounds': rounds,
        'cumulative_user_records': users,
        'blockers': blockers,
        'warnings': [] if unit else ['completion_time_unit_unverified_for_future_rollover'],
        'note': 'Confirmed means the existing worker stored a successful platform response; final release check still runs on Douyin.'
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--backend-directory', required=True)
    parser.add_argument('--environment-file')
    parser.add_argument('--runtime-url', default='http://127.0.0.1:8765/healthz')
    arguments = parser.parse_args()
    # Only a loopback health GET is permitted; this tool never publishes scores.
    if not re.fullmatch(r'http://127\.0\.0\.1:[0-9]{1,5}/healthz', arguments.runtime_url):
        print(json.dumps({'diagnostic_error': 'loopback_health_url_required'}))
        return 1
    try:
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), RejectRedirects())
        with opener.open(arguments.runtime_url, timeout=3) as response:
            data = response.read(65537)
            runtime = json.loads(data) if len(data) <= 65536 else None
    except Exception:
        runtime = None
    try:
        value = inspect_server(arguments.backend_directory, environment_file=arguments.environment_file, runtime_health=runtime)
        if runtime is None:
            value['warnings'].append('runtime_health_unavailable')
        print(json.dumps(value, ensure_ascii=False, indent=2))
        return 0
    except DiagnosticError as error:
        print(json.dumps({'diagnostic_error': str(error)}))
        return 1
    except Exception:
        print(json.dumps({'diagnostic_error': 'unexpected_diagnostic_failure'}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
