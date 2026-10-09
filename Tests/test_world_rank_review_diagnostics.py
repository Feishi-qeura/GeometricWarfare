import importlib.util
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, HTTPServer

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Services/LivePlatformBackend'))
from live_backend.store import Store

SPEC = importlib.util.spec_from_file_location('world_rank_diagnostics', ROOT / 'Scripts/WorldRankReviewKit/diagnostics.py')
diagnostics = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(diagnostics)


class ReviewDiagnosticsTests(unittest.TestCase):
    def setUp(self):
        test_directory = ROOT / 'Saved/WorldRankDiagnosticTests'
        test_directory.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=test_directory)
        self.root = Path(self.temp.name)
        assert self.root.resolve().is_relative_to(test_directory.resolve())
        self.database = self.root / 'live.sqlite3'
        self.store = Store(self.database)
        self.env = self.root / '.env'
        self.env.write_text('BACKEND_APP_ID=ttReview\nBACKEND_APP_SECRET=SECRET_MUST_NOT_APPEAR\nBACKEND_ONLINE_VERSION=false\nBACKEND_ENABLE_UPLOADS=false\nBACKEND_DATABASE_PATH=' + str(self.database) + '\n', encoding='utf-8')

    def tearDown(self):
        self.temp.cleanup()

    def job(self, operation, *, state='pending', app='ttReview', online=0, error=None):
        with self.store.connection(write=True) as db:
            db.execute('INSERT INTO outbox(publication,sequence,app_id,online,period,operation,body,state,not_before,lease_token,error,completed_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)',
                       (f'pub-{operation}-{state}-{app}-{online}', 0, app, online, 'week', operation, '{"open_id":"PRIVATE_USER","token":"PRIVATE_TOKEN"}', state, 0, 'PRIVATE_LEASE', error, 123 if state == 'done' else None))

    def inspect(self):
        return diagnostics.inspect_server(self.root, runtime_health={'uploads_enabled': False, 'complete_time_unit': 'pending_verification', 'status': 'ready'})

    def test_disabled_uploads_and_exact_two_api_evidence_are_distinct(self):
        self.job('set_valid_version')
        self.job('upload_user_result')
        self.job('upload_rank_list')
        value = self.inspect()
        self.assertIn('uploads_disabled', value['blockers'])
        self.assertEqual(value['outbox']['pending'], 3)
        self.assertEqual(value['review_apis']['upload_user_result']['confirmed_jobs'], 0)
        self.assertEqual(value['review_apis']['upload_rank_list']['confirmed_jobs'], 0)
        self.assertEqual(value['head_job']['operation'], 'set_valid_version')

    def test_success_counts_are_scoped_and_never_expose_credentials_or_payload(self):
        self.job('upload_user_result', state='done')
        self.job('upload_rank_list', state='done', app='another-app')
        self.job('upload_rank_list', state='done', online=1)
        value = self.inspect()
        self.assertEqual(value['review_apis']['upload_user_result']['confirmed_jobs'], 1)
        self.assertEqual(value['review_apis']['upload_rank_list']['confirmed_jobs'], 0)
        text = json.dumps(value)
        for sensitive in ['SECRET_MUST_NOT_APPEAR', 'PRIVATE_USER', 'PRIVATE_TOKEN', 'PRIVATE_LEASE']:
            self.assertNotIn(sensitive, text)

    def test_completion_head_and_failed_head_are_not_bypassed(self):
        self.job('complete_upload_user_result')
        self.assertIn('completion_time_unit_required', self.inspect()['blockers'])
        with self.store.connection(write=True) as db:
            db.execute("UPDATE outbox SET state='failed',error='platform_40001'")
        self.assertIn('head_job_failed', self.inspect()['blockers'])
        self.assertEqual(self.inspect()['head_job']['error'], 'platform_40001')

    def test_configuration_change_requires_existing_worker_restart(self):
        self.env.write_text(self.env.read_text().replace('BACKEND_ENABLE_UPLOADS=false', 'BACKEND_ENABLE_UPLOADS=true'))
        value = self.inspect()
        self.assertNotIn('uploads_disabled', value['blockers'])
        self.assertIn('runtime_restart_required', value['blockers'])

    def test_read_only_inspection_does_not_create_or_change_database(self):
        self.job('upload_rank_list')
        before = self.database.read_bytes()
        self.inspect()
        self.assertEqual(before, self.database.read_bytes())
        self.database.unlink()
        with self.assertRaises(diagnostics.DiagnosticError):
            self.inspect()
        self.assertFalse(self.database.exists())

    def test_runtime_payload_is_filtered_and_invalid_config_is_sanitized(self):
        value = diagnostics.inspect_server(self.root, runtime_health={'uploads_enabled': False, 'app_secret': 'PRIVATE_TOKEN', 'outbox': {'pending': 3}, 'status': 'ready'})
        self.assertNotIn('PRIVATE_TOKEN', json.dumps(value))
        self.env.write_text(self.env.read_text().replace('BACKEND_ENABLE_UPLOADS=false', 'BACKEND_ENABLE_UPLOADS=tru'))
        with self.assertRaises(diagnostics.DiagnosticError):
            self.inspect()

    def test_loopback_health_redirect_is_never_followed(self):
        seen = []
        class RedirectingHealth(BaseHTTPRequestHandler):
            def do_GET(self):
                seen.append(self.path)
                if self.path == '/healthz':
                    self.send_response(302)
                    self.send_header('Location', '/outside-health-boundary')
                    self.end_headers()
                else:
                    self.send_response(200)
                    self.end_headers()
                    self.wfile.write(b'{"uploads_enabled":false,"status":"ready"}')
            def log_message(self, *args):
                pass
        server = HTTPServer(('127.0.0.1', 0), RedirectingHealth)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            environment = {**os.environ, 'HTTP_PROXY': '', 'HTTPS_PROXY': '', 'ALL_PROXY': '', 'NO_PROXY': '*'}
            result = subprocess.run([sys.executable, str(ROOT / 'Scripts/WorldRankReviewKit/diagnostics.py'), '--backend-directory', str(self.root), '--runtime-url', f'http://127.0.0.1:{server.server_port}/healthz'], capture_output=True, text=True, timeout=10, env=environment)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(seen, ['/healthz'])
            self.assertIn('runtime_health_unavailable', json.loads(result.stdout)['warnings'])
        finally:
            server.shutdown()
            server.server_close()
            worker.join(2)


if __name__ == '__main__':
    unittest.main()
