import unittest
from live_backend.worker import Worker
from live_backend.upstream import UpstreamError
import test_service


class PublishingAPI:
    def __init__(self, error=None):
        self.sent = []
        self.error = error
    def publish(self, operation, body):
        if self.error:
            raise self.error
        self.sent.append((operation, body))


class WorkerTests(unittest.TestCase):
    body = test_service.ServiceTests.body
    tearDown = test_service.ServiceTests.tearDown
    def setUp(self):
        test_service.ServiceTests.setUp(self)
        self.publisher = PublishingAPI()
        self.worker = Worker(self.backend, self.publisher)
        self.backend.settings.enable_uploads = True

    def test_missing_complete_unit_blocks_switch_but_new_week_can_accumulate(self):
        self.backend.submit(self.credential, self.body())
        self.now += 1200
        self.backend.rollover()
        self.backend.submit(self.credential, self.body("2"))
        for _ in range(6):
            self.worker.run_once()
        operations = [op for op, _ in self.publisher.sent]
        self.assertEqual(operations.count("set_valid_version"), 1)
        self.assertNotIn("complete_upload_user_result", operations)
        with self.store.connection() as db:
            blocked = db.execute("SELECT error FROM outbox WHERE operation='complete_upload_user_result'").fetchone()[0]
        self.assertEqual(blocked, "complete_time_unit_unconfigured")
        self.assertEqual(self.store.totals("app", False, "gw-week-20261004T230000")["user"]["score"], 7)

    def test_explicit_unit_sends_complete_before_new_active_version(self):
        self.backend.settings.complete_time_unit = "milliseconds"
        self.backend.submit(self.credential, self.body())
        self.now += 1200
        self.backend.rollover()
        while self.worker.run_once():
            pass
        operations = [op for op, _ in self.publisher.sent]
        self.assertEqual(operations[-2:], ["complete_upload_user_result", "set_valid_version"])
        complete = self.publisher.sent[-2][1]
        self.assertEqual(complete["complete_time"], self.now // 3600 * 3600 * 1000)

    def test_two_legal_scores_overflow_week_int64_is_quarantined(self):
        self.backend.submit(self.credential, self.body("1", score="9223372036854775807"))
        self.backend.submit(self.credential, self.body("2", score="9223372036854775807"))
        for _ in range(4):
            self.worker.run_once()
        self.assertEqual(self.store.totals("app", False, "gw-week-20260927T230000")["user"]["score"], 18446744073709551614)
        self.assertEqual(self.store.outbox_status().get("failed"), 1)
        self.assertTrue(all(u["score"] <= 9223372036854775807 for _, body in self.publisher.sent for u in body.get("user_list", []) + body.get("rank_list", [])))

    def test_transient_error_retries_same_job_and_terminal_error_is_retained(self):
        self.backend.submit(self.credential, self.body())
        self.publisher.error = UpstreamError("platform_4014034")
        self.worker.run_once()
        self.assertEqual(self.store.outbox_status(), {"pending": 3})
        self.assertFalse(self.worker.run_once())
        self.now += 60
        self.publisher.error = UpstreamError("platform_40001", transient=False)
        self.worker.run_once()
        self.assertEqual(self.store.outbox_status().get("failed"), 1)

    def test_retry_after_errcode_fix_preserves_scores_and_drains_in_order(self):
        from live_backend.upstream import DouyinAPI
        from test_upstream import CaptureTransport, FIXTURE
        self.backend.submit(self.credential, self.body())
        original_totals = self.store.totals('app', False, 'gw-week-20260927T230000')
        with self.store.connection(write=True) as db:
            job_id = db.execute('SELECT min(id) FROM outbox').fetchone()[0]
            db.execute("UPDATE outbox SET state='failed',attempts=1,error='upstream_response_shape' WHERE id=?", (job_id,))
        transport = CaptureTransport([FIXTURE['application_token']['response'], {'errcode': 0}, {'errcode': 0}, {'errcode': 0}])
        api = DouyinAPI('app', 'FAKE_SECRET', transport=transport)
        worker = Worker(self.backend, api)
        self.assertFalse(worker.run_once())
        self.assertEqual(transport.calls, [])
        self.assertTrue(self.store.retry_failed(job_id, now=self.now))
        while worker.run_once(): pass
        operations = [call[0].rsplit('/', 1)[-1] for call in transport.calls[1:]]
        self.assertEqual(operations, ['set_valid_version', 'upload_user_result', 'upload_rank_list'])
        self.assertEqual(self.store.outbox_status(), {'done': 3})
        self.assertEqual(self.store.totals('app', False, 'gw-week-20260927T230000'), original_totals)
        with self.store.connection() as db:
            self.assertEqual(db.execute('SELECT count(*) FROM rounds').fetchone()[0], 1)


if __name__ == "__main__":
    unittest.main()
