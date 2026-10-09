import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from live_backend.store import Store, Conflict, Unauthorized


class StoreTests(unittest.TestCase):
    def setUp(self):
        test_root = Path(__file__).resolve().parents[1] / ".test-data"
        test_root.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=test_root)
        self.db = Path(self.temp.name) / "backend.sqlite3"
        self.store = Store(self.db)
        self.identity = {"app_id": "app", "room_id": "room", "anchor_id": "anchor", "online": False}

    def tearDown(self):
        self.temp.cleanup()

    def submit(self, round_id="1", score=7, jobs=None):
        return self.store.archive_round(
            self.identity, round_id, "period-a", {"round_id": round_id, "score": str(score)},
            {"user": {"score": score}}, jobs or [], now=100,
        )

    def test_same_round_is_exactly_once_but_conflicting_payload_rejected(self):
        self.assertTrue(self.submit()["created"])
        self.assertFalse(self.submit()["created"])
        with self.assertRaises(Conflict):
            self.submit(score=8)
        self.assertEqual(self.store.totals("app", False, "period-a"), {"user": {"score": 7}})
        reopened = Store(self.db)
        self.assertEqual(reopened.totals("app", False, "period-a"), {"user": {"score": 7}})

    def test_large_totals_do_not_overflow_sqlite_or_lose_precision(self):
        self.submit("1", 9223372036854775807)
        self.submit("2", 9223372036854775807)
        self.assertEqual(self.store.totals("app", False, "period-a")["user"]["score"], 18446744073709551614)
        self.assertEqual(self.store.totals("app", True, "period-a"), {})

    def test_duplicate_identity_from_different_anchor_is_conflict(self):
        self.submit()
        self.identity["anchor_id"] = "another-anchor"
        with self.assertRaises(Conflict):
            self.submit()

    def test_archive_and_jobs_rollback_when_publication_builder_fails(self):
        def fail(_):
            raise RuntimeError("synthetic rollback")
        with self.assertRaises(RuntimeError):
            self.store.archive_round(self.identity, "1", "period-a", {"score": "7"}, {"user": {"score": 7}}, fail, now=100)
        self.assertEqual(self.store.totals("app", False, "period-a"), {})
        self.assertTrue(self.submit()["created"])

    def test_session_is_bound_to_verified_identity_and_expires(self):
        credential = self.store.create_session(self.identity, expires_at=200, now=100)
        self.assertEqual(self.store.authenticate(credential, now=199), self.identity)
        with self.assertRaises(Unauthorized):
            self.store.authenticate(credential, now=200)
        with self.assertRaises(Unauthorized):
            self.store.authenticate("invented", now=100)
        with self.db.open("rb") as file:
            self.assertNotIn(credential.encode(), file.read())

    def test_outbox_orders_snapshots_and_fences_expired_lease(self):
        self.submit("1", jobs=[{"operation": "set_valid_version", "body": {"v": "a"}}, {"operation": "upload_user_result", "body": {"score": "7"}}])
        self.submit("2", jobs=[{"operation": "upload_rank_list", "body": {"score": "14"}}])
        first = self.store.claim_job(now=100, lease_seconds=5)
        self.assertEqual(first["operation"], "set_valid_version")
        self.assertIsNone(self.store.claim_job(now=101))
        replacement = self.store.claim_job(now=106)
        self.assertEqual(replacement["id"], first["id"])
        self.assertFalse(self.store.finish_job(first["id"], first["lease_token"], now=107))
        self.assertTrue(self.store.finish_job(replacement["id"], replacement["lease_token"], now=107))
        second = self.store.claim_job(now=108)
        self.assertEqual(second["operation"], "upload_user_result")
        self.assertTrue(self.store.retry_job(second["id"], second["lease_token"], "upstream_unavailable", now=108, delay=10))
        self.assertIsNone(self.store.claim_job(now=109))
        retry = self.store.claim_job(now=118)
        self.assertEqual(retry["body"], second["body"])
        self.store.finish_job(retry["id"], retry["lease_token"], now=119)
        third = self.store.claim_job(now=120)
        self.assertEqual(third["operation"], "upload_rank_list")

    def test_permanent_failure_blocks_newer_overwrite(self):
        self.submit("1", jobs=[{"operation": "upload_user_result", "body": {"score": "7"}}])
        self.submit("2", jobs=[{"operation": "upload_rank_list", "body": {"score": "14"}}])
        job = self.store.claim_job(now=100)
        self.store.fail_job(job["id"], job["lease_token"], "upstream_rejected", now=101)
        self.assertIsNone(self.store.claim_job(now=10000))
        self.assertEqual(self.store.outbox_status(), {"pending": 1, "failed": 1})

    def test_simultaneous_duplicate_requests_commit_only_once(self):
        with ThreadPoolExecutor(max_workers=6) as pool:
            receipts = list(pool.map(lambda _: self.submit(), range(12)))
        self.assertEqual(sum(r["created"] for r in receipts), 1)
        self.assertEqual(self.store.totals("app", False, "period-a"), {"user": {"score": 7}})

    def test_only_one_local_upload_process_may_hold_instance_lock(self):
        from live_backend.instance import InstanceLock
        path = self.db.with_suffix(".lock")
        first = InstanceLock(path).acquire()
        try:
            with self.assertRaises(RuntimeError):
                InstanceLock(path).acquire()
        finally:
            first.release()
        resumed = InstanceLock(path).acquire()
        resumed.release()

    def test_wal_aware_backup_restores_committed_round_totals(self):
        from live_backend.operator import backup_database
        destination = self.db.with_name("backup.sqlite3")
        with self.store.connection() as reader:
            reader.execute("BEGIN")
            reader.execute("SELECT count(*) FROM rounds").fetchone()
            self.submit()
            self.assertTrue(Path(str(self.db) + "-wal").exists())
            backup_database(self.store, destination)
        restored = Store(destination)
        self.assertEqual(restored.totals("app", False, "period-a"), {"user": {"score": 7}})
        self.assertFalse(restored.archive_round(self.identity, "1", "period-a", {"round_id": "1", "score": "7"}, {"user": {"score": 7}}, [], now=101)["created"])


if __name__ == "__main__":
    unittest.main()
