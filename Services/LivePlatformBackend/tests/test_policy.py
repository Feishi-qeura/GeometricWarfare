import unittest
from datetime import datetime, timezone

from live_backend.policy import week_period, reduce_results, receipt_users, publication_jobs, validate_round


def timestamp(iso):
    return int(datetime.fromisoformat(iso).timestamp())


class PolicyTests(unittest.TestCase):
    def test_sunday_2300_boundary_uses_hong_kong_not_server_timezone(self):
        before = week_period(timestamp("2026-10-04T22:59:59+08:00"))
        after = week_period(timestamp("2026-10-04T23:00:00+08:00"))
        self.assertNotEqual(before["version"], after["version"])
        self.assertEqual(before["end"], after["start"])
        self.assertEqual(after["version"], "gw-week-20261004T230000")

    def test_score_win_count_and_loss_reset_are_separate(self):
        history = [{"contribution": {"score": 10, "result": 1}}, {"contribution": {"score": 7, "result": 2}}, {"contribution": {"score": 5, "result": 1}}]
        self.assertEqual(reduce_results(history), {"score": 22, "winning_points": 2, "winning_streak_count": 1})

    def test_publication_batches_users_and_sends_top150_once(self):
        totals = {f"user-{i:03}": {"score": i, "winning_points": 1, "winning_streak_count": 1} for i in range(174)}
        jobs = publication_jobs("app", False, "week", totals, set(totals))
        user_jobs = [j for j in jobs if j["operation"] == "upload_user_result"]
        self.assertEqual([len(j["body"]["user_list"]) for j in user_jobs], [50, 50, 50, 24])
        rank = [j for j in jobs if j["operation"] == "upload_rank_list"]
        self.assertEqual(len(rank), 1)
        self.assertEqual(len(rank[0]["body"]["rank_list"]), 150)
        self.assertEqual(rank[0]["body"]["rank_list"][0]["score"], 173)
        self.assertTrue(all(j["body"]["world_rank_version"] == "week" for j in jobs))

    def test_gray_ignored_and_user_ids_unique(self):
        body = {"app_id": "app", "room_id": "123", "round_id": "1", "start_time": 1700000000, "end_time": 1700000100, "users": [{"open_id": "gray", "group_id": "gray", "score": "0", "result": 3}]}
        normalized, contributions = validate_round(body, {"red": "red", "blue": "blue", "gray": "gray"}, now=1700000101)
        self.assertEqual(contributions, {})
        body["users"] *= 2
        with self.assertRaises(ValueError):
            validate_round(body, {"red": "red", "blue": "blue", "gray": "gray"}, now=1700000101)

    def test_red_blue_result_consistency_and_negative_score_rejected(self):
        users = [{"open_id": "a", "group_id": "red", "score": "2", "result": 1}, {"open_id": "b", "group_id": "blue", "score": "1", "result": 1}]
        body = {"app_id": "app", "room_id": "123", "round_id": "1", "start_time": 1700000000, "end_time": 1700000100, "users": users}
        with self.assertRaises(ValueError):
            validate_round(body, {"red": "red", "blue": "blue", "gray": "gray"}, now=1700000101)
        users[1]["result"] = 2
        users[0]["score"] = -1
        with self.assertRaises(ValueError):
            validate_round(body, {"red": "red", "blue": "blue", "gray": "gray"}, now=1700000101)


if __name__ == "__main__":
    unittest.main()
