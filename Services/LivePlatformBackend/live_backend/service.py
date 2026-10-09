"""Authenticated round accounting without client-provided weekly totals."""
from dataclasses import dataclass, field
import os
import time
from .policy import identifier, integer, week_period, validate_round, reduce_results, receipt_users, publication_jobs
from .store import Unauthorized


def boolean_env(name, default="false"):
    value = os.getenv(name, default).lower()
    if value not in ("true", "false"):
        raise ValueError("invalid_boolean_configuration")
    return value == "true"


class RoundPending(Exception):
    """Retry the frozen round after the actual server-side week boundary."""

@dataclass
class Settings:
    app_id: str = ""
    app_secret: str = field(default="", repr=False)
    red_group: str = "Red"
    blue_group: str = "Blue"
    gray_group: str = "Grey"
    online: bool = False
    session_ttl: int = 43200
    database_path: str = "data/live-backend.sqlite3"
    allow_loopback_http: bool = False
    enable_uploads: bool = False
    complete_time_unit: str = ""

    @classmethod
    def from_env(cls):
        unit = os.getenv("BACKEND_COMPLETE_TIME_UNIT", "")
        if unit not in ("", "seconds", "milliseconds"):
            raise ValueError("invalid_completion_unit")
        ttl = int(os.getenv("BACKEND_SESSION_TTL_SECONDS", "43200"))
        if not 300 <= ttl <= 43200:
            raise ValueError("invalid_session_ttl")
        return cls(app_id=os.getenv("BACKEND_APP_ID", ""), app_secret=os.getenv("BACKEND_APP_SECRET", ""),
                   red_group=os.getenv("BACKEND_RED_GROUP", "Red"), blue_group=os.getenv("BACKEND_BLUE_GROUP", "Blue"), gray_group=os.getenv("BACKEND_GRAY_GROUP", "Grey"),
                   online=boolean_env("BACKEND_ONLINE_VERSION"), session_ttl=ttl,
                   database_path=os.getenv("BACKEND_DATABASE_PATH", "data/live-backend.sqlite3"),
                   allow_loopback_http=boolean_env("BACKEND_ALLOW_LOOPBACK_HTTP"),
                   enable_uploads=boolean_env("BACKEND_ENABLE_UPLOADS"), complete_time_unit=unit)

class Backend:
    def __init__(self, settings, repository, api, clock=time.time):
        self.settings, self.repository, self.api, self.clock = settings, repository, api, clock

    def exchange(self, launch_token, body):
        if not isinstance(body, dict) or set(body) != {"app_id", "room_id", "anchor_open_id"}:
            raise ValueError("invalid_session_fields")
        app_id, anchor = identifier(body["app_id"]), identifier(body["anchor_open_id"])
        room_id = str(integer(body["room_id"], string_only=True, positive=True))
        if app_id != self.settings.app_id:
            raise Unauthorized("identity_mismatch")
        trusted = self.api.verify_launch(launch_token)
        if trusted != {"app_id": app_id, "room_id": room_id, "anchor_id": anchor}:
            raise Unauthorized("identity_mismatch")
        identity = {**trusted, "online": self.settings.online}
        now = int(self.clock())
        expiry = now + self.settings.session_ttl
        credential = self.repository.create_session(identity, expires_at=expiry, now=now)
        return {"app_id": app_id, "room_id": room_id, "anchor_open_id": anchor, "session_token": credential, "expires_at": expiry}

    def close_jobs(self, period, totals):
        jobs = publication_jobs(self.settings.app_id, self.settings.online, period["version"], totals)
        # Store a logical cutoff. Conversion to verified seconds/milliseconds
        # occurs only when the operator explicitly supplies the unit.
        jobs.append({"operation": "complete_upload_user_result", "body": {"app_id": self.settings.app_id,
                     "is_online_version": self.settings.online, "world_rank_version": period["version"], "_complete_time_seconds": period["end_time"]}})
        return jobs

    def submit(self, credential, body):
        now = int(self.clock())
        identity = self.repository.authenticate(credential, now=now)
        normalized, contributions = validate_round(body, {"red": self.settings.red_group, "blue": self.settings.blue_group, "gray": self.settings.gray_group}, now=now)
        if identity["app_id"] != normalized["app_id"] or identity["room_id"] != normalized["room_id"] or identity["online"] != self.settings.online:
            raise Unauthorized("identity_mismatch")
        ended_period = week_period(normalized["end_time"])
        if ended_period["start"] > now:
            raise RoundPending("round_time_pending")
        # User-confirmed: first successful server receipt determines the week,
        # so a completed round delayed over cutoff is never silently excluded.
        period = {}
        def choose_receiving_period(transaction_time):
            # The database may wait for another writer across cutoff. Choose
            # once after its write lock is acquired, never before that wait.
            period.update(week_period(transaction_time))
            return {**period, "late_arrival": ended_period["version"] != period["version"]}
        result = self.repository.archive_round(identity, normalized["round_id"], "", normalized, contributions,
            lambda totals: publication_jobs(self.settings.app_id, self.settings.online, period["version"], totals), now=now,
            reducer=reduce_results, period_info=choose_receiving_period, close_builder=self.close_jobs, clock=self.clock,
            receipt_builder=lambda histories, late: {"accepted": True, "round_id": normalized["round_id"], "world_rank_version": period["version"],
                 "users": receipt_users(normalized, histories), "late": late, "operator_state": "late_round_received_current_week" if late else "world_upload_queued"})
        return {**result["receipt"], "duplicate": not result["created"]}

    def rollover(self):
        now = int(self.clock())
        self.repository.rollover(self.settings.app_id, self.settings.online, week_period(now), now=now, close_builder=self.close_jobs)
