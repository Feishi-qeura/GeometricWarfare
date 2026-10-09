"""Isolated OpenAPI mock inbox. No SDK sessions, gameplay or leaderboard APIs."""
import argparse
from contextlib import closing
import base64
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import sqlite3
import threading
import time

BODY_LIMIT = 1024 * 1024
KINDS = {'live_comment', 'live_like', 'live_gift', 'live_fansclub'}
SIGNED_KEYS = ('x-msg-type', 'x-nonce-str', 'x-roomid', 'x-timestamp')
FIELDS = {'msg_id', 'sec_openid', 'content', 'nickname', 'avatar_url', 'timestamp',
          'sec_gift_id', 'gift_num', 'gift_value', 'test', 'audience_sec_open_id',
          'sec_magic_gift_id', 'like_num', 'fansclub_reason_type', 'fansclub_level'}


class Reject(Exception):
    def __init__(self, status, code):
        self.status, self.code = status, code
        super().__init__(code)


def signature(headers, body, secret):
    prefix = '&'.join(key + '=' + headers[key] for key in SIGNED_KEYS).encode('utf-8')
    return base64.b64encode(hashlib.md5(prefix + body + secret.encode('utf-8')).digest()).decode('ascii')


def unique_fields(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate_field')
        result[key] = value
    return result


class Inbox:
    max_events = 10000

    def __init__(self, database, room, secret):
        if not re.fullmatch(r'1[0-9]{18}', room) or not secret:
            raise ValueError('mock_room_or_push_secret_required')
        self.database, self.room, self.secret = str(database), room, secret
        Path(database).parent.mkdir(parents=True, exist_ok=True)
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute('CREATE TABLE IF NOT EXISTS mock_events (room TEXT, kind TEXT, id TEXT, '
                       'digest TEXT, received INTEGER, payload TEXT, PRIMARY KEY(room,kind,id))')

    def receive(self, headers, body):
        if len(body) > BODY_LIMIT:
            raise Reject(413, 'body_limit')
        if any(not headers.get(key) or len(headers[key]) > 256 for key in SIGNED_KEYS):
            raise Reject(400, 'missing_or_invalid_headers')
        if headers['x-roomid'] != self.room or headers['x-msg-type'] not in KINDS:
            raise Reject(403, 'mock_room_or_type_mismatch')
        try:
            if abs(int(headers['x-timestamp']) - int(time.time() * 1000)) > 300000:
                raise ValueError()
        except ValueError:
            raise Reject(401, 'timestamp_outside_five_minutes')
        if not hmac.compare_digest(signature(headers, body, self.secret), headers.get('x-signature', '')):
            raise Reject(401, 'invalid_signature')
        try:
            events = json.loads(body.decode('utf-8'), object_pairs_hook=unique_fields,
                                parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
            if not isinstance(events, list) or not 1 <= len(events) <= 5000:
                raise ValueError()
            records = []
            for event in events:
                if not isinstance(event, dict):
                    raise ValueError()
                for key in ('msg_id', 'sec_openid'):
                    if not isinstance(event.get(key), str) or not 1 <= len(event[key]) <= 256:
                        raise ValueError()
                count_key = {'live_like': 'like_num', 'live_gift': 'gift_num'}.get(headers['x-msg-type'])
                if count_key:
                    count = event.get(count_key)
                    if isinstance(count, bool) or not isinstance(count, int) or not 1 <= count <= 9223372036854775807:
                        raise ValueError()
                if headers['x-msg-type'] == 'live_comment' and not isinstance(event.get('content'), str):
                    raise ValueError()
                if headers['x-msg-type'] == 'live_gift' and not isinstance(event.get('sec_gift_id'), str):
                    raise ValueError()
                # Store only documented mock fields, never headers or credentials.
                clean = {key: value for key, value in event.items() if key in FIELDS}
                payload = json.dumps(clean, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False)
                if len(payload.encode('utf-8')) > 16384:
                    raise ValueError()
                records.append((event['msg_id'], hashlib.sha256(payload.encode('utf-8')).hexdigest(), payload))
        except (ValueError, TypeError, UnicodeError, RecursionError):
            raise Reject(422, 'invalid_mock_payload')
        accepted = duplicates = 0
        with closing(sqlite3.connect(self.database, timeout=1)) as db, db:
            db.execute('BEGIN IMMEDIATE')
            total = db.execute('SELECT count(*) FROM mock_events').fetchone()[0]
            for event_id, digest, payload in records:
                previous = db.execute('SELECT digest FROM mock_events WHERE room=? AND kind=? AND id=?',
                                      (self.room, headers['x-msg-type'], event_id)).fetchone()
                if previous:
                    if previous[0] != digest:
                        raise Reject(409, 'conflicting_mock_replay')
                    duplicates += 1
                    continue
                if total + accepted >= self.max_events:
                    raise Reject(507, 'mock_inbox_full')
                db.execute('INSERT INTO mock_events VALUES(?,?,?,?,?,?)',
                           (self.room, headers['x-msg-type'], event_id, digest, int(time.time() * 1000), payload))
                accepted += 1
        return dict(accepted=accepted, duplicates=duplicates, mock_only=True)

    def status(self):
        with closing(sqlite3.connect(self.database)) as db, db:
            counts = dict(db.execute('SELECT kind,count(*) FROM mock_events GROUP BY kind'))
        return dict(events=sum(counts.values()), by_type=counts, room_id=self.room, mock_only=True)

    def recent(self):
        with closing(sqlite3.connect(self.database)) as db, db:
            rows = db.execute('SELECT kind,received,payload FROM mock_events ORDER BY rowid DESC LIMIT 20').fetchall()
        result = []
        for kind, received, payload in rows:
            event = json.loads(payload)
            result.append(dict(kind=kind, received=received, msg_id=event['msg_id'],
                               nickname=event.get('nickname', ''), content=event.get('content', ''),
                               gift_id=event.get('sec_gift_id', ''),
                               count=str(event.get('gift_num', event.get('like_num', ''))), mock_only=True))
        return result


def serve(inbox, port):
    class Handler(BaseHTTPRequestHandler):
        def setup(self):
            super().setup()
            self.connection.settimeout(10)

        def log_message(self, *_):
            pass

        def reply(self, status, data, head=False):
            body = json.dumps(data).encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            if not head:
                self.wfile.write(body)

        def do_HEAD(self):
            self.health(True)

        def do_GET(self):
            self.health(False)

        def health(self, head):
            if self.path not in ('/', '/healthz'):
                self.reply(404, dict(error='not_found'), head)
            else:
                self.reply(200, dict(status='mock_receiver_ready', mock_only=True), head)

        def do_POST(self):
            try:
                if self.path not in ('/', '/push'):
                    raise Reject(404, 'not_found')
                if self.headers.get('Transfer-Encoding'):
                    raise Reject(400, 'content_length_required')
                if self.headers.get('Content-Type', '').split(';')[0].lower() != 'application/json':
                    raise Reject(415, 'json_required')
                try:
                    size = int(self.headers.get('Content-Length', '-1'))
                except ValueError:
                    size = -1
                if not 0 < size <= BODY_LIMIT:
                    raise Reject(413, 'body_limit')
                for key in (*SIGNED_KEYS, 'x-signature'):
                    if len(self.headers.get_all(key, [])) != 1:
                        raise Reject(400, 'missing_or_duplicate_headers')
                body = self.rfile.read(size)
                if len(body) != size:
                    raise Reject(400, 'incomplete_body')
                self.reply(200, inbox.receive(self.headers, body))
            except Reject as error:
                self.reply(error.status, dict(error=error.code, mock_only=True))
            except sqlite3.Error:
                self.reply(503, dict(error='mock_storage_unavailable'))
            except (OSError, TimeoutError):
                self.close_connection = True
            except Exception:
                self.reply(500, dict(error='mock_receiver_failure'))

    class Server(ThreadingHTTPServer):
        daemon_threads = True
        slots = threading.BoundedSemaphore(8)

        def process_request(self, request, client_address):
            if not self.slots.acquire(False):
                self.shutdown_request(request)
                return
            try:
                super().process_request(request, client_address)
            except Exception:
                self.slots.release()
                raise

        def process_request_thread(self, request, client_address):
            try:
                super().process_request_thread(request, client_address)
            finally:
                self.slots.release()

    with Server(('127.0.0.1', port), Handler) as server:
        server.serve_forever()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=('serve', 'status', 'recent'))
    parser.add_argument('--config', default=str(Path(__file__).with_name('settings.json')))
    args = parser.parse_args()
    settings = json.loads(Path(args.config).read_text(encoding='utf-8-sig'))
    inbox = Inbox(settings['database'], settings['room_id'], settings['push_secret'])
    if args.command == 'serve':
        serve(inbox, int(settings.get('port', 8766)))
    else:
        print(json.dumps(getattr(inbox, args.command)(), ensure_ascii=False, indent=2))
