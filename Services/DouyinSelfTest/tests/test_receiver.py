import base64
import hashlib
import json
from pathlib import Path
import tempfile
import time
import unittest

from receiver import Inbox, Reject, signature

ROOM = '1000000000000000000'


class ReceiverTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=Path(__file__).parent)
        self.inbox = Inbox(Path(self.tmp.name) / 'mock.sqlite3', ROOM, 'default')

    def tearDown(self):
        self.tmp.cleanup()

    def message(self, kind='live_comment', **fields):
        event = dict(msg_id='fixture-1', sec_openid='mock-user', content='加入', **fields)
        body = json.dumps([event], ensure_ascii=False).encode('utf-8')
        headers = {'x-roomid': ROOM, 'x-msg-type': kind, 'x-nonce-str': 'fixture',
                   'x-timestamp': str(int(time.time() * 1000))}
        headers['x-signature'] = signature(headers, body, 'default')
        return headers, body

    def test_official_signature_fixture(self):
        headers = {'x-nonce-str': '123456', 'x-timestamp': '456789',
                   'x-roomid': '268', 'x-msg-type': 'live_gift'}
        self.assertEqual(signature(headers, 'abc123你好'.encode(), '123abc'), 'PDcKhdlsrKEJif6uMKD2dw==')

    def test_signed_unicode_receipt_and_duplicate(self):
        headers, body = self.message()
        self.assertEqual(self.inbox.receive(headers, body), {'accepted': 1, 'duplicates': 0, 'mock_only': True})
        self.assertEqual(self.inbox.receive(headers, body)['duplicates'], 1)
        self.assertEqual(self.inbox.status()['events'], 1)
        self.assertEqual(self.inbox.recent()[0]['content'], '加入')

    def test_tampered_payload_does_not_enter_inbox(self):
        headers, body = self.message()
        with self.assertRaises(Reject) as error:
            self.inbox.receive(headers, body.replace('加入'.encode(), '退出'.encode()))
        self.assertEqual(error.exception.status, 401)
        self.assertEqual(self.inbox.status()['events'], 0)

    def test_other_room_and_unknown_type_rejected(self):
        headers, body = self.message()
        headers['x-roomid'] = '1000000000000000001'
        headers['x-signature'] = signature(headers, body, 'default')
        with self.assertRaises(Reject):
            self.inbox.receive(headers, body)
        headers, body = self.message('live_unknown')
        with self.assertRaises(Reject):
            self.inbox.receive(headers, body)

    def test_expired_header_rejected(self):
        headers, body = self.message()
        headers['x-timestamp'] = '1'
        headers['x-signature'] = signature(headers, body, 'default')
        with self.assertRaises(Reject):
            self.inbox.receive(headers, body)

    def test_conflicting_replay_is_not_success(self):
        headers, body = self.message()
        self.inbox.receive(headers, body)
        changed = body.replace('加入'.encode(), '退出'.encode())
        headers['x-signature'] = signature(headers, changed, 'default')
        with self.assertRaises(Reject) as error:
            self.inbox.receive(headers, changed)
        self.assertEqual(error.exception.status, 409)
        self.assertEqual(self.inbox.recent()[0]['content'], '加入')

    def test_atomic_invalid_batch(self):
        headers, body = self.message()
        body = json.dumps([json.loads(body)[0], {'msg_id': 'bad'}]).encode()
        headers['x-signature'] = signature(headers, body, 'default')
        with self.assertRaises(Reject):
            self.inbox.receive(headers, body)
        self.assertEqual(self.inbox.status()['events'], 0)

    def test_exact_int64_gift_and_like(self):
        for kind, fields in [('live_gift', dict(sec_gift_id='gift=', gift_num=9223372036854775807)),
                             ('live_like', dict(like_num=9007199254740993))]:
            headers, body = self.message(kind, **fields)
            self.inbox.receive(headers, body)
        self.assertEqual(self.inbox.status()['events'], 2)
        self.assertEqual(self.inbox.recent()[0]['count'], '9007199254740993')

    def test_bounded_storage_and_duplicate_at_capacity(self):
        self.inbox.max_events = 1
        headers, body = self.message()
        self.inbox.receive(headers, body)
        self.assertEqual(self.inbox.receive(headers, body)['duplicates'], 1)
        body = body.replace(b'fixture-1', b'fixture-2')
        headers['x-signature'] = signature(headers, body, 'default')
        with self.assertRaises(Reject) as error:
            self.inbox.receive(headers, body)
        self.assertEqual(error.exception.status, 507)

    def test_duplicate_json_keys_rejected(self):
        headers, _ = self.message()
        body = b'[{"msg_id":"a","msg_id":"b","sec_openid":"mock"}]'
        headers['x-signature'] = signature(headers, body, 'default')
        with self.assertRaises(Reject):
            self.inbox.receive(headers, body)


if __name__ == '__main__':
    unittest.main()
