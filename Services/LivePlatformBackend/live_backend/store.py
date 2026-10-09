"""SQLite local repository: atomic archives, lossless totals and fenced outbox."""
import hashlib
import json
import secrets
import sqlite3
from contextlib import contextmanager
from pathlib import Path


WORLD_OPERATIONS = frozenset({"set_valid_version", "upload_user_result", "upload_rank_list", "complete_upload_user_result"})


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def additive(history):
    result = {}
    for item in history:
        for key, value in item["contribution"].items():
            if type(value) is not int:
                raise ValueError("integer metric required")
            result[key] = result.get(key, 0) + value
    return result

class Conflict(Exception):
    pass


class Unauthorized(Exception):
    pass


class Store:
    def __init__(self, path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.connection() as db:
            db.execute("PRAGMA journal_mode=WAL")
            db.executescript("""
                CREATE TABLE IF NOT EXISTS sessions (
                    credential_hash TEXT PRIMARY KEY, identity TEXT NOT NULL,
                    created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS rounds (
                    id INTEGER PRIMARY KEY AUTOINCREMENT, app_id TEXT NOT NULL,
                    online INTEGER NOT NULL, room_id TEXT NOT NULL, round_id TEXT NOT NULL,
                    anchor_id TEXT NOT NULL, period TEXT NOT NULL, digest TEXT NOT NULL,
                    payload TEXT NOT NULL, end_time INTEGER NOT NULL, received_at INTEGER NOT NULL,
                    receipt TEXT, late INTEGER NOT NULL DEFAULT 0,
                    UNIQUE(app_id, online, room_id, round_id)
                );
                CREATE TABLE IF NOT EXISTS round_users (
                    archive_id INTEGER NOT NULL REFERENCES rounds(id), open_id TEXT NOT NULL,
                    contribution TEXT NOT NULL, PRIMARY KEY(archive_id, open_id)
                );
                CREATE TABLE IF NOT EXISTS totals (
                    app_id TEXT NOT NULL, online INTEGER NOT NULL, period TEXT NOT NULL,
                    open_id TEXT NOT NULL, metrics TEXT NOT NULL,
                    PRIMARY KEY(app_id, online, period, open_id)
                );
                CREATE TABLE IF NOT EXISTS periods (
                    app_id TEXT NOT NULL, online INTEGER NOT NULL, version TEXT NOT NULL,
                    start_time INTEGER NOT NULL, end_time INTEGER NOT NULL, state TEXT NOT NULL,
                    PRIMARY KEY(app_id,online,version)
                );
                CREATE TABLE IF NOT EXISTS outbox (
                    id INTEGER PRIMARY KEY AUTOINCREMENT, publication TEXT NOT NULL,
                    sequence INTEGER NOT NULL, app_id TEXT NOT NULL, online INTEGER NOT NULL,
                    period TEXT NOT NULL, operation TEXT NOT NULL, body TEXT NOT NULL,
                    state TEXT NOT NULL DEFAULT 'pending', attempts INTEGER NOT NULL DEFAULT 0,
                    not_before INTEGER NOT NULL, lease_until INTEGER NOT NULL DEFAULT 0,
                    lease_token TEXT, error TEXT, completed_at INTEGER,
                    UNIQUE(publication, sequence)
                );
                CREATE INDEX IF NOT EXISTS rounds_period ON rounds(app_id,online,period,end_time);
                CREATE INDEX IF NOT EXISTS rounds_users ON round_users(open_id,archive_id);
                CREATE INDEX IF NOT EXISTS outbox_delivery ON outbox(state,not_before,id);
                PRAGMA user_version=1;
            """)

    @contextmanager
    def connection(self, write=False):
        db = sqlite3.connect(self.path, timeout=10, isolation_level=None)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA foreign_keys=ON")
        db.execute("PRAGMA busy_timeout=10000")
        try:
            if write:
                db.execute("BEGIN IMMEDIATE")
            yield db
            if write:
                db.commit()
        except BaseException:
            if write:
                db.rollback()
            raise
        finally:
            db.close()

    def create_session(self, identity, *, expires_at, now):
        if expires_at <= now:
            raise ValueError("invalid session expiry")
        credential = secrets.token_urlsafe(32)
        digest = hashlib.sha256(credential.encode()).hexdigest()
        with self.connection(write=True) as db:
            db.execute("DELETE FROM sessions WHERE expires_at<=?", (now,))
            db.execute("INSERT INTO sessions VALUES(?,?,?,?)", (digest, canonical(identity), now, expires_at))
        return credential

    def authenticate(self, credential, *, now):
        if not isinstance(credential, str) or not 20 <= len(credential) <= 256:
            raise Unauthorized("invalid_session")
        digest = hashlib.sha256(credential.encode()).hexdigest()
        with self.connection() as db:
            row = db.execute("SELECT identity,expires_at FROM sessions WHERE credential_hash=?", (digest,)).fetchone()
        if not row or row["expires_at"] <= now:
            raise Unauthorized("invalid_session")
        return json.loads(row["identity"])

    def archive_round(self, identity, round_id, period, payload, contributions, jobs, *, now, reducer=None,
                      receipt_builder=None, period_info=None, close_builder=None, late_arrival=False, clock=None):
        app_id, online, room_id = identity["app_id"], int(identity["online"]), identity["room_id"]
        anchor_id = identity["anchor_id"]
        # Identity and policy inputs are part of the immutable archive hash too.
        # Reception period is derived server state, not submitted content.
        # A retry across cutoff must return the original receipt, not conflict.
        digest = hashlib.sha256(canonical([identity, payload, contributions]).encode()).hexdigest()
        with self.connection(write=True) as db:
            prior = db.execute("SELECT id,digest,period,receipt,late FROM rounds WHERE app_id=? AND online=? AND room_id=? AND round_id=?",
                               (app_id, online, room_id, round_id)).fetchone()
            if prior:
                if prior["digest"] != digest:
                    raise Conflict("round_conflict")
                return {"created": False, "publication_id": f"round:{prior['id']}", "period": prior["period"],
                        "receipt": json.loads(prior["receipt"]) if prior["receipt"] else None, "late": bool(prior["late"])}
            if clock:
                now = int(clock())
            if callable(period_info):
                period_info = period_info(now)
                period = period_info["version"]
                late_arrival = period_info.get("late_arrival", late_arrival)
            late = bool(late_arrival)
            if period_info is not None:
                self._prepare_period(db, app_id, online, period_info, now, close_builder)
                if db.execute("SELECT state FROM periods WHERE app_id=? AND online=? AND version=?", (app_id, online, period)).fetchone()["state"] == "sealed":
                    raise ValueError("sealed_period_accounting_rejected")
            end_time = payload.get("end_time", now)
            archive_id = db.execute("INSERT INTO rounds(app_id,online,room_id,round_id,anchor_id,period,digest,payload,end_time,received_at,late) VALUES(?,?,?,?,?,?,?,?,?,?,?)",
                                    (app_id, online, room_id, round_id, anchor_id, period, digest, canonical(payload), end_time, now, int(late))).lastrowid
            histories = {}
            for open_id, contribution in contributions.items():
                db.execute("INSERT INTO round_users VALUES(?,?,?)", (archive_id, open_id, canonical(contribution)))
                rows = db.execute("SELECT r.end_time,r.room_id,r.round_id,u.contribution FROM round_users u JOIN rounds r ON r.id=u.archive_id WHERE r.app_id=? AND r.online=? AND r.period=? AND u.open_id=?",
                                  (app_id, online, period, open_id)).fetchall()
                history = [{**dict(row), "contribution": json.loads(row["contribution"])} for row in rows]
                # Round IDs are decimal strings; numeric tie ordering preserves precision.
                history.sort(key=lambda row: (row["end_time"], row["room_id"], int(row["round_id"])))
                histories[open_id] = history
                metrics = (reducer or additive)(history)
                db.execute("INSERT INTO totals VALUES(?,?,?,?,?) ON CONFLICT(app_id,online,period,open_id) DO UPDATE SET metrics=excluded.metrics",
                           (app_id, online, period, open_id, canonical(metrics)))
            publication = f"round:{archive_id}"
            snapshot = self._totals(db, app_id, bool(online), period)
            publication_jobs = jobs(snapshot) if callable(jobs) else jobs
            self._enqueue(db, publication, app_id, online, period, publication_jobs, now)
            receipt = receipt_builder(histories, late) if receipt_builder else None
            db.execute("UPDATE rounds SET receipt=? WHERE id=?", (canonical(receipt), archive_id))
            return {"created": True, "publication_id": publication, "period": period, "receipt": receipt, "late": late}

    def _prepare_period(self, db, app_id, online, period, now, close_builder):
        if period["start"] > now:
            raise ValueError("future_period_activation_rejected")
        expired = db.execute("SELECT * FROM periods WHERE app_id=? AND online=? AND state='open' AND end_time<=? ORDER BY start_time", (app_id, online, now)).fetchall()
        for row in expired:
            db.execute("UPDATE periods SET state='sealed' WHERE app_id=? AND online=? AND version=?", (app_id, online, row["version"]))
            snapshot = self._totals(db, app_id, bool(online), row["version"])
            closing = close_builder(dict(row), snapshot) if close_builder else []
            self._enqueue(db, f"close:{app_id}:{online}:{row['version']}", app_id, online, row["version"], closing, now)
        prior = db.execute("SELECT 1 FROM periods WHERE app_id=? AND online=? AND version=?", (app_id, online, period["version"])).fetchone()
        if not prior:
            sealed = period["end"] <= now
            db.execute("INSERT INTO periods VALUES(?,?,?,?,?,?)", (app_id, online, period["version"], period["start"], period["end"], "sealed" if sealed else "open"))
            if not sealed:
                self._enqueue(db, f"open:{app_id}:{online}:{period['version']}", app_id, online, period["version"], [{"operation": "set_valid_version", "body": {"app_id": app_id, "is_online_version": bool(online), "world_rank_version": period["version"]}}], now)

    def rollover(self, app_id, online, period, *, now, close_builder):
        with self.connection(write=True) as db:
            self._prepare_period(db, app_id, int(online), period, now, close_builder)

    def _totals(self, db, app_id, online, period):
        return {row["open_id"]: json.loads(row["metrics"]) for row in db.execute(
            "SELECT open_id,metrics FROM totals WHERE app_id=? AND online=? AND period=?", (app_id, int(online), period))}

    def totals(self, app_id, online, period):
        with self.connection() as db:
            return self._totals(db, app_id, online, period)

    def _enqueue(self, db, publication, app_id, online, period, jobs, now):
        for sequence, job in enumerate(jobs):
            if job["operation"] not in WORLD_OPERATIONS:
                raise ValueError("invalid_world_operation")
            db.execute("INSERT INTO outbox(publication,sequence,app_id,online,period,operation,body,not_before) VALUES(?,?,?,?,?,?,?,?)",
                       (publication, sequence, app_id, online, period, job["operation"], canonical(job["body"]), now))

    def claim_job(self, *, now, lease_seconds=30):
        with self.connection(write=True) as db:
            row = db.execute("""SELECT j.* FROM outbox j WHERE
                ((j.state='pending' AND j.not_before<=?) OR (j.state='leased' AND j.lease_until<=?))
                AND NOT EXISTS(SELECT 1 FROM outbox p WHERE p.app_id=j.app_id AND p.online=j.online AND p.id<j.id AND p.state!='done')
                ORDER BY j.id LIMIT 1""", (now, now)).fetchone()
            if not row:
                return None
            token = secrets.token_urlsafe(24)
            db.execute("UPDATE outbox SET state='leased',lease_token=?,lease_until=?,attempts=attempts+1 WHERE id=?",
                       (token, now + lease_seconds, row["id"]))
            return {**dict(row), "state": "leased", "body": json.loads(row["body"]), "lease_token": token, "attempts": row["attempts"] + 1}

    def _transition(self, job_id, lease_token, state, *, now, error=None, delay=0):
        with self.connection(write=True) as db:
            return bool(db.execute("""UPDATE outbox SET state=?,error=?,not_before=?,completed_at=?,lease_token=NULL,lease_until=0
                WHERE id=? AND state='leased' AND lease_token=? AND lease_until>?""",
                (state, error, now + delay, now if state == "done" else None, job_id, lease_token, now)).rowcount)

    def finish_job(self, job_id, lease_token, *, now):
        return self._transition(job_id, lease_token, "done", now=now)

    def retry_job(self, job_id, lease_token, error, *, now, delay):
        return self._transition(job_id, lease_token, "pending", now=now, error=error, delay=max(1, delay))

    def fail_job(self, job_id, lease_token, error, *, now):
        return self._transition(job_id, lease_token, "failed", now=now, error=error)

    def outbox_status(self):
        with self.connection() as db:
            return {row["state"]: row["n"] for row in db.execute("SELECT state,count(*) n FROM outbox GROUP BY state")}

    def operator_status(self):
        with self.connection() as db:
            errors = [dict(row) for row in db.execute("SELECT id,operation,state,attempts,error FROM outbox WHERE error IS NOT NULL ORDER BY id LIMIT 100")]
            late = db.execute("SELECT count(*) FROM rounds WHERE late=1").fetchone()[0]
        return {"outbox": self.outbox_status(), "late_archived": late, "job_diagnostics": errors}

    def retry_failed(self, job_id, *, now):
        with self.connection(write=True) as db:
            return bool(db.execute("UPDATE outbox SET state='pending',not_before=?,lease_token=NULL,lease_until=0 WHERE id=? AND state='failed'", (now, job_id)).rowcount)


SQLiteRepository = Store
