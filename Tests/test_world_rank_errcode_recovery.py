import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from contextlib import closing
import sqlite3

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Services/LivePlatformBackend'))
from live_backend.store import Store
SPEC = importlib.util.spec_from_file_location('errcode_recovery', ROOT / 'Scripts/WorldRankReviewKit/errcode_recovery.py')
recovery = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(recovery)


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / 'Saved/WorldRankRecoveryTests'
        directory.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=directory)
        self.root = Path(self.temp.name)
        assert self.root.resolve().is_relative_to(directory.resolve())
        self.database = self.root / 'scores.sqlite3'
        self.store = Store(self.database)
        (self.root / '.env').write_text('BACKEND_APP_ID=ttRecovery\nBACKEND_APP_SECRET=PRIVATE_SECRET\nBACKEND_ONLINE_VERSION=false\nBACKEND_ENABLE_UPLOADS=true\nBACKEND_DATABASE_PATH=' + str(self.database) + '\n')
        self.body = json.dumps({'app_id': 'ttRecovery', 'is_online_version': False, 'world_rank_version': 'fixture-version'})
        with self.store.connection(write=True) as db:
            db.execute("INSERT INTO outbox(publication,sequence,app_id,online,period,operation,body,state,attempts,not_before,error) VALUES('pub',0,'ttRecovery',0,'fixture-version','set_valid_version',?,'failed',1,0,'upstream_response_shape')", (self.body,))
            db.execute("INSERT INTO outbox(publication,sequence,app_id,online,period,operation,body,state,attempts,not_before) VALUES('pub',1,'ttRecovery',0,'fixture-version','upload_user_result','{}','pending',0,0)")
            db.execute("INSERT INTO totals(app_id,online,period,open_id,metrics) VALUES('ttRecovery',0,'fixture-version','PRIVATE_USER','{\"score\":7}')")
        self.backup = self.root / 'private-backup.sqlite3'
        self.backup.touch()

    def tearDown(self): self.temp.cleanup()

    def retry(self, job_id=1):
        with patch.dict(os.environ, {}, clear=True):
            return recovery.retry(self.root, None, job_id, self.backup)

    def test_requeue_only_exact_head_with_consistent_original_backup(self):
        result = self.retry()
        self.assertTrue(result['requeued'])
        self.assertNotIn('PRIVATE_', json.dumps(result))
        with self.store.connection() as db:
            states = [dict(row) for row in db.execute('SELECT id,state,attempts,error,body FROM outbox ORDER BY id')]
            totals = db.execute('SELECT metrics FROM totals').fetchone()[0]
        self.assertEqual(states[0]['state'], 'pending')
        self.assertEqual(states[0]['attempts'], 1)
        self.assertEqual(states[0]['body'], self.body)
        self.assertEqual(states[1]['attempts'], 0)
        self.assertEqual(totals, '{"score":7}')
        with closing(sqlite3.connect(self.backup)) as db:
            self.assertEqual(db.execute('SELECT state FROM outbox WHERE id=1').fetchone()[0], 'failed')
            self.assertEqual(db.execute('SELECT metrics FROM totals').fetchone()[0], totals)

    def test_wrong_id_error_or_app_never_writes_queue(self):
        with self.assertRaises(ValueError): self.retry(2)
        self.assertEqual(self.backup.stat().st_size, 0)
        with self.store.connection(write=True) as db: db.execute("UPDATE outbox SET error='platform_40001' WHERE id=1")
        with self.assertRaises(ValueError): self.retry()
        with self.store.connection(write=True) as db: db.execute("UPDATE outbox SET error='upstream_response_shape',app_id='another-app' WHERE id=1")
        with self.assertRaises(ValueError): self.retry()
        self.assertEqual(self.backup.stat().st_size, 0)

    def test_formal_environment_and_disabled_uploads_are_blocked(self):
        env_file = self.root / '.env'
        original = env_file.read_text()
        env_file.write_text(original.replace('BACKEND_ONLINE_VERSION=false', 'BACKEND_ONLINE_VERSION=true'))
        with self.assertRaises(ValueError): self.retry()
        env_file.write_text(original.replace('BACKEND_ENABLE_UPLOADS=true', 'BACKEND_ENABLE_UPLOADS=false'))
        with self.assertRaises(ValueError): self.retry()
        self.assertEqual(self.backup.stat().st_size, 0)

    def test_nonempty_backup_is_never_overwritten_and_second_retry_is_blocked(self):
        self.backup.write_bytes(b'KEEP_EXISTING_SNAPSHOT')
        with self.assertRaises(ValueError): self.retry()
        self.assertEqual(self.backup.read_bytes(), b'KEEP_EXISTING_SNAPSHOT')
        self.backup.write_bytes(b'')
        self.retry()
        another = self.root / 'second-backup.sqlite3'
        another.touch()
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaises(ValueError): recovery.retry(self.root, None, 1, another)
        self.assertEqual(another.stat().st_size, 0)


if __name__ == '__main__': unittest.main()
