import json
import unittest
from pathlib import Path

from live_backend.upstream import DouyinAPI, UpstreamError, validate_world_body, check_errors

FIXTURE = json.loads((Path(__file__).resolve().parent / "fixtures/douyin-world-rank-public-contract.json").read_text(encoding="utf-8"))


class CaptureTransport:
    def __init__(self, responses):
        self.responses = list(responses)
        self.calls = []

    def post(self, url, headers, body):
        self.calls.append((url, headers, body))
        return self.responses.pop(0)


class UpstreamTests(unittest.TestCase):
    def api(self, responses):
        transport = CaptureTransport([FIXTURE["application_token"]["response"], *responses])
        return DouyinAPI("ttContractFixture", "FAKE_SERVER_SECRET_NOT_REAL", transport=transport), transport

    def test_room_identity_preserves_native_int64_and_uses_application_header(self):
        api, transport = self.api([FIXTURE["room_exchange"]["normal_response_without_error_envelope"]])
        room = api.verify_launch("FAKE_LAUNCH_TOKEN_NOT_REAL")
        self.assertEqual(room["room_id"], "7214015683695250235")
        self.assertEqual(room["anchor_id"], "_000fixture_anchor")
        self.assertEqual(transport.calls[1][1]["x-token"], "FAKE_APP_TOKEN_NOT_REAL")
        self.assertEqual(transport.calls[1][2], {"token": "FAKE_LAUNCH_TOKEN_NOT_REAL"})
        self.assertNotIn("secret", transport.calls[1][2])

    def test_nonzero_error_rejected_even_with_real_shape(self):
        for body in FIXTURE["room_exchange"]["must_reject_responses"]:
            api, _ = self.api([body])
            with self.assertRaises(UpstreamError):
                api.verify_launch("fake")

    def test_world_wire_uses_exact_integer_and_requires_explicit_success(self):
        body = FIXTURE["world_rank"]["operation_fixtures"][1]["body"]
        api, transport = self.api([FIXTURE["world_rank"]["success_response"]])
        api.publish("upload_user_result", body)
        self.assertEqual(transport.calls[-1][2]["user_list"][0]["score"], 9007199254740993)
        self.assertIs(type(transport.calls[-1][2]["user_list"][0]["score"]), int)
        for invalid in [{}, {"err_no": 40001}, {"err_no": "0"}, {"err_no": 0, "extra": {"error_code": 9}}]:
            api, _ = self.api([invalid])
            with self.assertRaises(UpstreamError):
                api.publish("upload_user_result", body)

    def test_world_batch_and_int64_boundaries_rejected_before_send(self):
        body = FIXTURE["world_rank"]["operation_fixtures"][1]["body"]
        overflow = json.loads(json.dumps(body))
        overflow["user_list"][0]["score"] = 9223372036854775808
        with self.assertRaises(ValueError):
            validate_world_body("upload_user_result", overflow)
        overflow["user_list"] = [body["user_list"][0]] * 51
        with self.assertRaises(ValueError):
            validate_world_body("upload_user_result", overflow)

    def test_server_observed_errcode_zero_is_explicit_world_success_for_all_operations(self):
        # Server response probe: HTTP200, JSONdict, integer errcode=0 and no
        # err_no. The metadata contains no token, user identity or raw message.
        for fixture in FIXTURE['world_rank']['operation_fixtures']:
            api, transport = self.api([{'errcode': 0, 'errmsg': 'success'}])
            api.publish(fixture['path'].rsplit('/', 1)[-1], fixture['body'])
            self.assertEqual(len(transport.calls), 2)

    def test_explicit_success_requires_a_known_integer_code_and_rejects_any_error(self):
        check_errors({'errcode': 0}, explicit=True)
        for invalid in ({}, {'errmsg': 'success'}, {'errcode': '0'}, {'errcode': False}, {'errcode': None}, {'errcode': 0.0}, {'errcode': 0, 'err_no': 40001}, {'err_no': 0, 'errcode': 40001}, {'errcode': 0, 'extra': {'error_code': 9}}, {'errcode': 0, 'err_no': '0'}):
            with self.subTest(invalid=invalid):
                with self.assertRaises(UpstreamError): check_errors(invalid, explicit=True)


if __name__ == "__main__":
    unittest.main()
