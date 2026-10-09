import tempfile
import unittest
from pathlib import Path
from datetime import datetime

from live_backend.store import Store, Conflict, Unauthorized
from live_backend.service import Backend, Settings, RoundPending
from live_backend.policy import week_period


class FakeRoomAPI:
    def verify_launch(self, token):
        return {"app_id": "app", "room_id": "7214015683695250235", "anchor_id": "anchor"}


class ServiceTests(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).resolve().parents[1] / ".test-data"
        root.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=root)
        self.store = Store(Path(self.temp.name) / "db.sqlite3")
        self.now = int(datetime.fromisoformat("2026-10-04T22:50:00+08:00").timestamp())
        self.backend = Backend(Settings(app_id="app", app_secret="fake", red_group="Red", blue_group="Blue", gray_group="Grey"), self.store, FakeRoomAPI(), clock=lambda: self.now)
        self.identity = {"app_id": "app", "room_id": "7214015683695250235", "anchor_open_id": "anchor"}
        self.credential = self.backend.exchange("fake-launch", self.identity)["session_token"]

    def tearDown(self):
        self.temp.cleanup()

    def body(self, round_id="1", end=None, result=1, score="7"):
        completed = self.now if end is None else end
        return {"app_id": "app", "room_id": self.identity["room_id"], "round_id": round_id, "start_time": completed - 420, "end_time": completed,
                "users": [{"open_id": "user", "group_id": "Red", "score": score, "result": result}, {"open_id": "gray", "group_id": "Grey", "score": "0", "result": 3}]}

    def test_session_claimed_anchor_app_and_room_are_checked(self):
        for field in self.identity:
            with self.assertRaises(Unauthorized):
                self.backend.exchange("fake", {**self.identity, field: "7214015683695250236" if field == "room_id" else "wrong"})

    def test_retry_receipt_is_stable_and_streak_is_at_that_round(self):
        first = self.backend.submit(self.credential, self.body())
        self.assertEqual(first["users"], [{"open_id": "gray", "win_points": 0, "win_streak": 0}, {"open_id": "user", "win_points": 1, "win_streak": 1}])
        self.backend.submit(self.credential, self.body("2", end=self.now + 1))
        duplicate = self.backend.submit(self.credential, self.body())
        self.assertEqual(duplicate["users"], first["users"])
        self.assertTrue(duplicate["duplicate"])
        with self.assertRaises(Conflict):
            self.backend.submit(self.credential, self.body(score="8"))

    def test_out_of_order_loss_recomputes_final_streak_without_double_score(self):
        self.backend.submit(self.credential, self.body("3", end=self.now, result=1))
        self.backend.submit(self.credential, self.body("1", end=self.now - 2, result=1))
        self.backend.submit(self.credential, self.body("2", end=self.now - 1, result=2))
        totals = self.store.totals("app", False, "gw-week-20260927T230000")
        self.assertEqual(totals["user"], {"score": 21, "winning_points": 2, "winning_streak_count": 1})
        self.assertNotIn("gray", totals)

    def test_crossweek_late_first_receipt_counts_in_new_week_without_loss(self):
        receipt = self.backend.submit(self.credential, self.body())
        self.now += 1200
        self.backend.rollover()
        late = self.backend.submit(self.credential, self.body("2", end=self.now - 1200))
        self.assertTrue(late["late"])
        self.assertEqual(late["operator_state"], "late_round_received_current_week")
        self.assertEqual(late["world_rank_version"], "gw-week-20261004T230000")
        self.assertEqual(self.store.totals("app", False, late["world_rank_version"])["user"]["score"], 7)
        self.assertEqual(self.store.totals("app", False, receipt["world_rank_version"])["user"]["score"], 7)
        fresh = self.backend.submit(self.credential, self.body("3"))
        self.assertFalse(fresh["late"])
        self.assertEqual(fresh["users"][-1]["win_streak"], 2)

    def test_retry_across_week_keeps_original_receipt_and_modified_score_conflicts(self):
        body = self.body()
        original = self.backend.submit(self.credential, body)
        self.now += 1200
        self.backend.rollover()
        duplicate = self.backend.submit(self.credential, body)
        self.assertEqual(duplicate, {**original, "duplicate": True})
        self.assertEqual(self.store.totals("app", False, "gw-week-20261004T230000"), {})
        with self.assertRaises(Conflict):
            self.backend.submit(self.credential, {**body, "users": [{**u, "score": "9"} if u["open_id"] == "user" else u for u in body["users"]]})

    def test_first_newweek_loss_resets_and_gray_does_not_count(self):
        self.backend.submit(self.credential, self.body())
        self.now += 1200
        loss = self.backend.submit(self.credential, self.body("2", result=2))
        self.assertEqual(loss["users"][-1], {"open_id": "user", "win_points": 0, "win_streak": 0})
        win = self.backend.submit(self.credential, self.body("3", end=self.now + 1))
        self.assertEqual(win["users"][-1]["win_streak"], 1)
        self.assertEqual(self.store.totals("app", False, win["world_rank_version"])["user"]["winning_points"], 1)
        self.assertNotIn("gray", self.store.totals("app", False, win["world_rank_version"]))

    def test_receiving_period_is_chosen_after_database_write_lock(self):
        boundary = week_period(self.now)["end"]
        body = self.body(end=boundary - 60)
        times = iter([boundary - 1, boundary])
        self.backend.clock = lambda: next(times)
        receipt = self.backend.submit(self.credential, body)
        self.assertEqual(receipt["world_rank_version"], "gw-week-20261004T230000")
        with self.store.connection() as db:
            self.assertEqual(db.execute("SELECT received_at FROM rounds").fetchone()[0], boundary)

    def test_lifecycle_jobs_complete_old_before_new_version(self):
        self.backend.submit(self.credential, self.body())
        self.now += 1200
        self.backend.rollover()
        with self.store.connection() as db:
            ops = [r[0] for r in db.execute("SELECT operation FROM outbox ORDER BY id")]
        self.assertEqual(ops[-2:], ["complete_upload_user_result", "set_valid_version"])

    def test_future_end_time_cannot_activate_next_week_before_real_cutoff(self):
        self.backend.submit(self.credential, self.body())
        boundary = week_period(self.now)["end"]
        self.now = boundary - 30
        future_round = self.body("2", end=boundary)
        with self.assertRaises(RoundPending):
            self.backend.submit(self.credential, future_round)
        with self.store.connection() as db:
            versions = db.execute("SELECT count(*) FROM outbox WHERE operation='set_valid_version'").fetchone()[0]
            self.assertEqual(versions, 1)
        with self.assertRaises(ValueError):
            self.store.rollover("app", False, week_period(boundary), now=self.now, close_builder=self.backend.close_jobs)
        self.now = boundary
        self.backend.rollover()
        receipt = self.backend.submit(self.credential, future_round)
        self.assertTrue(receipt["accepted"])
        with self.store.connection() as db:
            ops = [r[0] for r in db.execute("SELECT operation FROM outbox ORDER BY id")]
        self.assertLess(ops.index("complete_upload_user_result"), len(ops) - 1 - ops[::-1].index("set_valid_version"))


if __name__ == "__main__":
    unittest.main()
