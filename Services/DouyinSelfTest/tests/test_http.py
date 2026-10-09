import http.client
from contextlib import closing
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest

from receiver import signature


class HttpTests(unittest.TestCase):
    def test_head_signed_push_and_private_inspection(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as folder:
            with socket.socket() as probe:
                probe.bind(('127.0.0.1', 0))
                port = probe.getsockname()[1]
            config = Path(folder) / 'settings.json'
            config.write_text(json.dumps(dict(room_id='1000000000000000000', push_secret='default',
                                             port=port, database=str(Path(folder) / 'mock.sqlite3'))))
            child = subprocess.Popen([sys.executable, str(root / 'receiver.py'), 'serve', '--config', str(config)],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            def request(method, path='/', body=None, headers=None):
                with closing(http.client.HTTPConnection('127.0.0.1', port, timeout=2)) as client:
                    client.request(method, path, body, headers or {})
                    response = client.getresponse()
                    return response.status, response.read()
            try:
                for attempt in range(40):
                    try:
                        self.assertEqual(request('HEAD'), (200, b''))
                        break
                    except ConnectionRefusedError:
                        time.sleep(.05)
                else:
                    self.fail('Receiver did not start')
                event = [{'msg_id': 'wire-1', 'sec_openid': 'mock', 'content': '加入'}]
                body = json.dumps(event, ensure_ascii=False).encode()
                headers = {'x-msg-type': 'live_comment', 'x-roomid': '1000000000000000000',
                           'x-nonce-str': 'fixture', 'x-timestamp': str(int(time.time() * 1000)),
                           'Content-Type': 'application/json'}
                headers['x-signature'] = signature(headers, body, 'default')
                status, response = request('POST', body=body, headers=headers)
                self.assertEqual(status, 200)
                self.assertEqual(json.loads(response)['accepted'], 1)
                self.assertEqual(json.loads(request('POST', body=body, headers=headers)[1])['duplicates'], 1)
                headers['x-signature'] = 'incorrect'
                self.assertEqual(request('POST', body=body, headers=headers)[0], 401)
                self.assertEqual(request('GET', '/events')[0], 404)
                self.assertNotIn(b'wire-1', request('GET')[1])
                result = subprocess.check_output([sys.executable, str(root / 'receiver.py'), 'status', '--config', str(config)])
                self.assertEqual(json.loads(result)['events'], 1)
            finally:
                child.terminate()
                child.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
