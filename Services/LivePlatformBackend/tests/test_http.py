import asyncio
import json
import unittest
import httpx
from unittest.mock import patch
import test_service
from live_backend.app import create_app, BODY_LIMIT
from live_backend.service import Settings


class HTTPTests(unittest.TestCase):
    body = test_service.ServiceTests.body
    tearDown = test_service.ServiceTests.tearDown

    def setUp(self):
        test_service.ServiceTests.setUp(self)
        self.app = create_app(backend=self.backend, start_worker=False)

    def request(self, path, body=None, *, credential=None, scheme="https", method="POST", size=None, client="127.0.0.1"):
        data = json.dumps(body or {}).encode()
        headers = [(b"content-type", b"application/json"), (b"content-length", str(len(data) if size is None else size).encode())]
        if credential is not None:
            headers.append((b"authorization", ("Bearer " + credential).encode()))
        async def run():
            transport = httpx.ASGITransport(app=self.app, client=(client, 4000))
            async with httpx.AsyncClient(transport=transport, base_url=f"{scheme}://localhost") as http:
                response = await http.request(method, path, content=data, headers=dict(headers))
                return response.status_code, response.json()
        return asyncio.run(asyncio.wait_for(run(), timeout=10))

    def test_full_exchange_receipt_duplicate_conflict_and_identity_rejection(self):
        code, session = self.request("/v1/douyin/sessions", self.identity, credential="fake-launch")
        self.assertEqual(code, 200)
        self.assertEqual(session["room_id"], self.identity["room_id"])
        token = session["session_token"]
        code, receipt = self.request("/v1/douyin/rounds", self.body(), credential=token)
        self.assertEqual(code, 200)
        self.assertTrue(receipt["accepted"])
        self.assertEqual(self.request("/v1/douyin/rounds", self.body(), credential=token)[1]["users"], receipt["users"])
        self.assertEqual(self.request("/v1/douyin/rounds", self.body(score="9"), credential=token)[0], 409)
        self.assertEqual(self.request("/v1/douyin/rounds", {**self.body(), "room_id": "123"}, credential=token)[0], 401)

    def test_transport_body_limit_and_no_auth_fail_closed(self):
        self.assertEqual(self.request("/v1/douyin/sessions", self.identity, credential="fake", scheme="http")[0], 403)
        self.assertEqual(self.request("/v1/douyin/sessions", self.identity, size=BODY_LIMIT + 1)[0], 413)
        self.assertEqual(self.request("/v1/douyin/rounds", self.body())[0], 401)
        self.backend.settings.allow_loopback_http = True
        self.assertEqual(self.request("/v1/douyin/sessions", self.identity, credential="fake", scheme="http")[0], 200)
        self.assertEqual(self.request("/v1/douyin/sessions", self.identity, credential="fake", scheme="http", client="198.51.100.1")[0], 403)

    def test_string_score_and_timestamp_order_contract(self):
        numeric = self.body()
        numeric["users"][0]["score"] = 7
        self.assertEqual(self.request("/v1/douyin/rounds", numeric, credential=self.credential)[0], 422)
        reversed_time = {**self.body(), "start_time": self.now + 1}
        self.assertEqual(self.request("/v1/douyin/rounds", reversed_time, credential=self.credential)[0], 422)

    def test_health_exposes_missing_unit_without_credentials_and_boolean_typos_rejected(self):
        status, value = self.request("/healthz", method="GET", scheme="http")
        self.assertEqual(status, 200)
        self.assertEqual(value["complete_time_unit"], "pending_verification")
        self.assertNotIn("fake", json.dumps(value))
        with patch.dict("os.environ", {"BACKEND_ONLINE_VERSION": "tru"}):
            with self.assertRaises(ValueError):
                Settings.from_env()

    def test_5000_maximum_ascii_identities_fit_four_mib_request_and_two_mib_receipt(self):
        self.backend.settings.red_group = "R" * 128
        body = self.body()
        body["users"] = [{"open_id": f"u-{i:05d}".ljust(256, "x"), "group_id": "R" * 128,
                          "score": "9223372036854775807", "result": 1} for i in range(5000)]
        self.assertGreater(len(json.dumps(body).encode()), 2 * 1024 * 1024)
        self.assertLess(len(json.dumps(body).encode()), BODY_LIMIT)
        status, receipt = self.request("/v1/douyin/rounds", body, credential=self.credential)
        self.assertEqual(status, 200)
        self.assertEqual(len(receipt["users"]), 5000)
        self.assertLess(len(json.dumps(receipt).encode()), 2 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()
