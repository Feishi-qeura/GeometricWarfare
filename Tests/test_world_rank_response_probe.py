import importlib.util
import json
import io
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Services/LivePlatformBackend'))
SPEC = importlib.util.spec_from_file_location('response_probe', ROOT / 'Scripts/WorldRankReviewKit/response_probe.py')
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


class ResponseShapeTests(unittest.TestCase):
    def test_token_and_message_values_and_dynamic_field_names_never_leak(self):
        value = {'err_no': 0, 'err_tips': 'PRIVATE_SECRET', 'data': {'access_token': 'PRIVATE_TOKEN', 'expires_in': 7200, 'PRIVATE_KEY': 'PRIVATE_USER'}, 'PRIVATE_DYNAMIC_KEY': 'anything'}
        summary = probe.summarize_response(value)
        output = json.dumps(summary)
        for secret in ('PRIVATE_SECRET', 'PRIVATE_TOKEN', 'PRIVATE_USER', 'PRIVATE_KEY', 'PRIVATE_DYNAMIC_KEY'):
            self.assertNotIn(secret, output)
        self.assertEqual(summary['fields']['err_no']['numeric_code'], 0)
        self.assertEqual(summary['fields']['data']['fields']['access_token']['type'], 'str')
        self.assertEqual(summary['unknown_field_count'], 1)

    def test_error_type_null_string_and_wrapper_remain_distinguishable(self):
        value = {'data': {'err_no': '0'}, 'error': None, 'err_msg': ''}
        summary = probe.summarize_response(value)
        self.assertEqual(summary['fields']['error']['type'], 'null')
        self.assertEqual(summary['fields']['data']['fields']['err_no']['type'], 'str')
        self.assertEqual(summary['fields']['data']['fields']['err_no']['numeric_code'], 0)
        self.assertNotIn('err_no', summary['fields'])

    def test_non_json_http200_is_reported_without_body_preview(self):
        record, parsed = probe.describe_http_response(200, 'text/html; charset=utf-8', b'<html>PRIVATE_TOKEN</html>')
        self.assertEqual(record['http_status'], 200)
        self.assertFalse(record['json_valid'])
        self.assertIsNone(parsed)
        self.assertNotIn('PRIVATE_TOKEN', json.dumps(record))

    def test_malformed_token_envelope_is_not_reclassified_as_success(self):
        from live_backend.upstream import DouyinAPI, UpstreamError
        class CapturingTransport:
            def __init__(self): self.records = []
            def post(self, url, headers, body):
                value = {'access_token': 'PRIVATE_TOKEN', 'expires_in': 7200}
                self.records.append(probe.summarize_response(value))
                return value
        transport = CapturingTransport()
        api = DouyinAPI('ttFixture', 'PRIVATE_SECRET', transport)
        with self.assertRaises(UpstreamError) as raised:
            api._application_token()
        self.assertEqual(raised.exception.code, 'upstream_response_shape')
        self.assertNotIn('PRIVATE_TOKEN', json.dumps(transport.records))


class ProbeFlowTests(unittest.TestCase):
    def setUp(self):
        from live_backend.store import Store
        test_root = ROOT / 'Saved/WorldRankResponseProbeTests'
        test_root.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=test_root)
        self.root = Path(self.temp.name)
        assert self.root.resolve().is_relative_to(test_root.resolve())
        self.database = self.root / 'scores.sqlite3'
        self.store = Store(self.database)
        (self.root / 'live_backend').mkdir()
        shutil.copyfile(ROOT / 'Services/LivePlatformBackend/live_backend/upstream.py', self.root / 'live_backend/upstream.py')
        self.body = {'app_id': 'ttProbe', 'is_online_version': False, 'world_rank_version': 'fixture-week'}
        with self.store.connection(write=True) as db:
            db.execute("INSERT INTO outbox(publication,sequence,app_id,online,period,operation,body,state,not_before,error) VALUES('pub',0,'ttProbe',0,'fixture-week','set_valid_version',?,'failed',0,'upstream_response_shape')", (json.dumps(self.body),))
        (self.root / '.env').write_text('BACKEND_APP_ID=ttProbe\nBACKEND_APP_SECRET=PRIVATE_SECRET\nBACKEND_ONLINE_VERSION=false\nBACKEND_DATABASE_PATH=' + str(self.database) + '\n', encoding='utf-8')

    def tearDown(self): self.temp.cleanup()

    def run_with_responses(self, responses):
        from live_backend.upstream import HTTPSJSONTransport
        calls = []
        class Response:
            status = 200
            headers = {'Content-Type': 'application/json'}
            def __init__(self, body): self.body = body
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, limit): return self.body[:limit]
        class Opener:
            def open(self, request, timeout):
                calls.append(request.full_url)
                response = responses[len(calls) - 1]
                if isinstance(response, Exception): raise response
                return Response(response if isinstance(response, bytes) else json.dumps(response).encode())
        def initialize(transport): transport.opener = Opener()
        before = self.database.read_bytes()
        with patch.dict('os.environ', {}, clear=True), patch.object(HTTPSJSONTransport, '__init__', initialize):
            report = probe.run_probe(self.root)
        self.assertEqual(before, self.database.read_bytes())
        self.assertNotIn('PRIVATE_SECRET', json.dumps(report))
        self.assertNotIn('PRIVATE_TOKEN', json.dumps(report))
        return report, calls

    def token(self): return {'err_no': 0, 'data': {'access_token': 'PRIVATE_TOKEN', 'expires_in': 7200}}

    def test_token_error_stops_before_world_request_and_keeps_failed_queue(self):
        report, calls = self.run_with_responses([{'data': {'access_token': 'PRIVATE_TOKEN', 'expires_in': 7200}}])
        self.assertEqual(len(calls), 1)
        self.assertEqual(report['error'], 'upstream_response_shape')
        self.assertEqual(report['requests'][0]['stage'], 'application_token')

    def test_valid_exact_two_endpoints_still_do_not_mark_queue_done(self):
        report, calls = self.run_with_responses([self.token(), {'err_no': 0, 'err_msg': 'PRIVATE_SECRET'}])
        self.assertEqual(calls, ['https://developer.toutiao.com/api/apps/v2/token', 'https://webcast.bytedance.com/api/gaming_con/world_rank/set_valid_version'])
        self.assertEqual(report['installed_backend_validation'], 'accepted')
        with self.store.connection() as db: self.assertEqual(db.execute('SELECT state FROM outbox').fetchone()[0], 'failed')

    def test_world_null_error_keeps_installed_strict_rejection(self):
        report, calls = self.run_with_responses([self.token(), {'err_no': 0, 'error': None}])
        self.assertEqual(len(calls), 2)
        self.assertEqual(report['error'], 'upstream_response_shape')
        self.assertEqual(report['requests'][1]['response_shape']['fields']['error']['type'], 'null')

    def test_http_error_metadata_never_prints_raw_body(self):
        error = urllib.error.HTTPError('https://example.invalid', 403, 'forbidden', {'Content-Type': 'text/html'}, io.BytesIO(b'PRIVATE_TOKEN PRIVATE_SECRET'))
        report, _ = self.run_with_responses([error])
        self.assertEqual(report['error'], 'upstream_http_403')
        self.assertEqual(report['requests'][0]['http_status'], 403)

    def test_oversized_response_is_bounded_and_redacted(self):
        report, _ = self.run_with_responses([b'PRIVATE_SECRET' + b'X' * (1024 * 1024)])
        self.assertEqual(report['error'], 'upstream_response_limit')
        self.assertTrue(report['requests'][0]['response_limit_exceeded'])

    def test_wrong_job_id_or_running_head_blocks_before_any_network(self):
        from live_backend.upstream import HTTPSJSONTransport
        with patch.dict('os.environ', {}, clear=True), patch.object(HTTPSJSONTransport, '__init__', side_effect=AssertionError('must not construct transport')):
            with self.assertRaises(ValueError): probe.run_probe(self.root, expected_job_id=2)
            with self.store.connection(write=True) as db: db.execute("UPDATE outbox SET state='pending'")
            with self.assertRaises(ValueError): probe.run_probe(self.root)


if __name__ == '__main__': unittest.main()
