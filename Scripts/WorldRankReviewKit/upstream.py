"""Official HTTPS wire contract verified against the public Go SDK fixtures."""
import json
import ssl
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

from .policy import MAX_INT64, identifier, integer
from .store import WORLD_OPERATIONS

TOKEN_URL = "https://developer.toutiao.com/api/apps/v2/token"
ROOM_URL = "https://webcast.bytedance.com/api/webcastmate/info"
WORLD_BASE = "https://webcast.bytedance.com/api/gaming_con/world_rank/"


class UpstreamError(Exception):
    def __init__(self, code="upstream_unavailable", transient=True):
        self.code = code
        self.transient = transient
        super().__init__(code)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise UpstreamError("upstream_redirect_rejected", transient=False)


class HTTPSJSONTransport:
    def __init__(self):
        # Certificate validation stays enabled. Proxy configuration belongs to
        # the server operator; destination URLs are not accepted from clients.
        self.opener = urllib.request.build_opener(NoRedirect(), urllib.request.HTTPSHandler(context=ssl.create_default_context()))

    def post(self, url, headers, body):
        if urllib.parse.urlsplit(url).scheme != "https":
            raise UpstreamError("https_required", transient=False)
        request = urllib.request.Request(url, data=json.dumps(body, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode(), headers=headers, method="POST")
        try:
            with self.opener.open(request, timeout=15) as response:
                if response.status != 200:
                    raise UpstreamError("upstream_http_failure", transient=response.status >= 500)
                data = response.read(1024 * 1024 + 1)
            if len(data) > 1024 * 1024:
                raise UpstreamError("upstream_response_limit", transient=False)
            value = json.loads(data)
            if not isinstance(value, dict):
                raise UpstreamError("upstream_response_shape", transient=False)
            return value
        except UpstreamError:
            raise
        except urllib.error.HTTPError as exc:
            # Do not read or forward the upstream body or exception URL.
            raise UpstreamError(f"upstream_http_{exc.code}", transient=exc.code in (401, 408, 425, 429) or exc.code >= 500) from None
        except (urllib.error.URLError, OSError, TimeoutError):
            raise UpstreamError("upstream_unavailable") from None
        except (ValueError, UnicodeError):
            raise UpstreamError("upstream_response_shape", transient=False) from None


def check_errors(body, *, explicit=False):
    if not isinstance(body, dict):
        raise UpstreamError("upstream_response_shape", transient=False)
    values = [body[key] for key in ("err_no", "errcode", "error_code", "errno", "error") if key in body]
    for parent, key in (("err", "err_code"), ("extra", "error_code")):
        if isinstance(body.get(parent), dict) and key in body[parent]:
            values.append(body[parent][key])
    # Live world-rank responses use errcode as well as the documented err_no.
    # Require an explicit recognized code; an empty body/message is not success.
    if explicit and not values:
        raise UpstreamError("upstream_response_shape", transient=False)
    for value in values:
        if type(value) is not int:
            raise UpstreamError("upstream_response_shape", transient=False)
        if value != 0:
            raise UpstreamError(f"platform_{value}" if abs(value) <= MAX_INT64 else "platform_error", transient=value in (40004, 4014034))


def validate_world_body(operation, body):
    if operation not in WORLD_OPERATIONS or not isinstance(body, dict):
        raise ValueError("invalid_world_operation")
    common = {"app_id", "is_online_version", "world_rank_version"}
    extra = {"upload_user_result": "user_list", "upload_rank_list": "rank_list", "complete_upload_user_result": "complete_time"}.get(operation)
    if set(body) != common | ({extra} if extra else set()):
        raise ValueError("invalid_world_fields")
    identifier(body["app_id"])
    identifier(body["world_rank_version"])
    if type(body["is_online_version"]) is not bool:
        raise ValueError("invalid_world_environment")
    if extra == "complete_time":
        integer(body[extra])
    if extra in ("user_list", "rank_list"):
        users = body[extra]
        if not isinstance(users, list) or len(users) > (50 if extra == "user_list" else 150):
            raise ValueError("world_user_limit")
        seen = set()
        for user in users:
            if not isinstance(user, dict) or set(user) != {"open_id", "rank", "score", "winning_points", "winning_streak_count"}:
                raise ValueError("invalid_world_user")
            open_id = identifier(user["open_id"])
            if open_id in seen:
                raise ValueError("duplicate_world_user")
            seen.add(open_id)
            for key in ("rank", "score", "winning_points", "winning_streak_count"):
                if type(user[key]) is not int:
                    raise ValueError("world_integer_required")
                integer(user[key], positive=key == "rank")


class DouyinAPI:
    def __init__(self, app_id, secret, transport=None):
        self.app_id = app_id
        self.secret = secret
        self.transport = transport or HTTPSJSONTransport()
        self._token = None
        self._token_until = 0
        self._lock = threading.Lock()
        self._publish_lock = threading.Lock()
        self._last_publish = 0
        self._room_lock = threading.Lock()
        self._last_room = 0

    def _application_token(self):
        if not self.app_id or not self.secret:
            raise UpstreamError("server_not_configured")
        with self._lock:
            if self._token and time.monotonic() < self._token_until:
                return self._token
            response = self.transport.post(TOKEN_URL, {"content-type": "application/json"}, {"appid": self.app_id, "secret": self.secret, "grant_type": "client_credential"})
            check_errors(response, explicit=True)
            data = response.get("data")
            if not isinstance(data, dict) or not isinstance(data.get("access_token"), str) or not data["access_token"] or type(data.get("expires_in")) is not int or data["expires_in"] <= 0:
                raise UpstreamError("upstream_token_shape", transient=False)
            self._token = data["access_token"]
            self._token_until = time.monotonic() + max(1, min(7200, data["expires_in"]) - 60)
            return self._token

    def _call(self, url, body):
        token = self._application_token()
        response = self.transport.post(url, {"content-type": "application/json", "x-token": token}, body)
        try:
            check_errors(response)
        except UpstreamError as error:
            if error.code == "platform_40004":
                with self._lock:
                    self._token_until = 0
            raise
        return response

    def verify_launch(self, token):
        if not isinstance(token, str) or not 1 <= len(token) <= 8192:
            raise UpstreamError("invalid_launch_token", transient=False)
        with self._room_lock:
            delay = .11 - (time.monotonic() - self._last_room)
            if delay > 0:
                time.sleep(delay)
            self._last_room = time.monotonic()
            response = self._call(ROOM_URL, {"token": token})
        data = response.get("data")
        info = data.get("info") if isinstance(data, dict) else None
        if not isinstance(info, dict) or type(info.get("room_id")) is not int or not 0 < info["room_id"] <= MAX_INT64:
            raise UpstreamError("invalid_trusted_room", transient=False)
        try:
            anchor = identifier(info.get("anchor_open_id"))
        except ValueError:
            raise UpstreamError("invalid_trusted_anchor", transient=False) from None
        # The room response has no app_id. This binding is the server's fixed
        # application credential context, not a platform-returned app proof.
        return {"app_id": self.app_id, "room_id": str(info["room_id"]), "anchor_id": anchor}

    def publish(self, operation, body):
        validate_world_body(operation, body)
        if body["app_id"] != self.app_id:
            raise ValueError("world_app_mismatch")
        with self._publish_lock:
            delay = .5 - (time.monotonic() - self._last_publish)
            if delay > 0:
                time.sleep(delay)
            self._last_publish = time.monotonic()
            response = self._call(WORLD_BASE + operation, body)
        check_errors(response, explicit=True)
