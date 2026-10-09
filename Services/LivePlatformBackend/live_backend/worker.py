"""Ordered durable delivery; completion units are never guessed."""
from .upstream import UpstreamError, validate_world_body


class Worker:
    def __init__(self, backend, api=None):
        self.backend = backend
        self.api = api or backend.api

    def run_once(self):
        settings = self.backend.settings
        if not settings.enable_uploads:
            return False
        repository = self.backend.repository
        now = int(self.backend.clock())
        job = repository.claim_job(now=now, lease_seconds=90)
        if not job:
            return False
        body = dict(job["body"])
        if job["operation"] == "complete_upload_user_result" and "_complete_time_seconds" in body:
            if settings.complete_time_unit not in ("seconds", "milliseconds"):
                repository.retry_job(job["id"], job["lease_token"], "complete_time_unit_unconfigured", now=now, delay=60)
                return False
            body["complete_time"] = body.pop("_complete_time_seconds") * (1000 if settings.complete_time_unit == "milliseconds" else 1)
        try:
            validate_world_body(job["operation"], body)
            if body["app_id"] != settings.app_id or body["is_online_version"] != settings.online:
                raise ValueError("world_identity_mismatch")
            self.api.publish(job["operation"], body)
        except ValueError:
            repository.fail_job(job["id"], job["lease_token"], "world_contract_invalid", now=int(self.backend.clock()))
        except UpstreamError as error:
            if error.transient:
                delay = min(3600, 2 ** min(job["attempts"], 12))
                repository.retry_job(job["id"], job["lease_token"], error.code, now=int(self.backend.clock()), delay=delay)
            else:
                repository.fail_job(job["id"], job["lease_token"], error.code, now=int(self.backend.clock()))
        else:
            repository.finish_job(job["id"], job["lease_token"], now=int(self.backend.clock()))
        return True
