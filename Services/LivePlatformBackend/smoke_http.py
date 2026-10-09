"""Actual loopback uvicorn/IIS-header smoke; no live auth or system changes."""
import json
from pathlib import Path
import socket
import tempfile
import threading
import time
import urllib.error
import urllib.request
import uvicorn

from live_backend.app import create_app
from live_backend.service import Backend, Settings
from live_backend.store import Store
from live_backend.upstream import DouyinAPI


def main():
    root = Path(__file__).resolve().parent / ".test-data"
    root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=root) as directory:
        database = str(Path(directory) / "smoke.sqlite3")
        settings = Settings(database_path=database)
        backend = Backend(settings, Store(database), DouyinAPI("", ""))
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        server = uvicorn.Server(uvicorn.Config(create_app(backend=backend), host="127.0.0.1", port=port,
                    access_log=False, log_level="error", proxy_headers=True, forwarded_allow_ips="127.0.0.1", root_path="/live-api"))
        thread = threading.Thread(target=server.run)
        thread.start()
        try:
            deadline = time.monotonic() + 10
            while not server.started and time.monotonic() < deadline:
                time.sleep(.02)
            if not server.started:
                raise RuntimeError("server_start_timeout")
            base = f"http://127.0.0.1:{port}"
            with urllib.request.urlopen(base + "/healthz", timeout=3) as response:
                health = json.load(response)
            if health["status"] != "configuration_required":
                raise RuntimeError("unexpected_health")
            statuses = []
            for forwarded in (False, True):
                headers = {"content-type": "application/json"}
                if forwarded:
                    headers["x-forwarded-proto"] = "https"
                request = urllib.request.Request(base + "/v1/douyin/sessions", data=b"{}", headers=headers)
                try:
                    urllib.request.urlopen(request, timeout=3)
                    raise RuntimeError("unauthorized_request_accepted")
                except urllib.error.HTTPError as error:
                    statuses.append(error.code)
            if statuses != [403, 401]:
                raise RuntimeError("unexpected_transport_policy")
            print(json.dumps({"actual_uvicorn_http": "passed", "bind": "127.0.0.1", "plain_auth": statuses[0],
                              "trusted_loopback_https_header_without_auth": statuses[1], "health": health["status"],
                              "live_platform_calls": 0}))
        finally:
            server.should_exit = True
            thread.join(40)
            if thread.is_alive():
                raise RuntimeError("server_shutdown_timeout")


if __name__ == "__main__":
    main()
