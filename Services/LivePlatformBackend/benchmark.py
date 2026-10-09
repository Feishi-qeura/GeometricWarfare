"""One local 5000-participant archive/receipt benchmark, no real upstream."""
import json
import platform
from pathlib import Path
import tempfile
import time

from live_backend.service import Backend, Settings
from live_backend.store import Store


class FixtureRoom:
    def verify_launch(self, token):
        return {"app_id": "benchmark", "room_id": "123", "anchor_id": "fixture-anchor"}


def main():
    root = Path(__file__).resolve().parent / ".test-data"
    root.mkdir(exist_ok=True)
    now = int(time.time())
    with tempfile.TemporaryDirectory(dir=root) as directory:
        path = Path(directory) / "benchmark.sqlite3"
        store = Store(path)
        service = Backend(Settings(app_id="benchmark", app_secret="FAKE_NOT_REAL"), store, FixtureRoom(), clock=lambda: now)
        session = service.exchange("FAKE_NOT_REAL", {"app_id": "benchmark", "room_id": "123", "anchor_open_id": "fixture-anchor"})
        body = {"app_id": "benchmark", "room_id": "123", "round_id": "1", "start_time": now - 420, "end_time": now,
                "users": [{"open_id": f"fixture-{i:05d}", "group_id": "Red" if i % 2 else "Blue", "score": str(i), "result": 1 if i % 2 else 2} for i in range(5000)]}
        started = time.perf_counter()
        receipt = service.submit(session["session_token"], body)
        elapsed = time.perf_counter() - started
        retry_started = time.perf_counter()
        duplicate = service.submit(session["session_token"], body)
        retry_elapsed = time.perf_counter() - retry_started
        print(json.dumps({"scope": "local Windows workstation; not Tencent server capacity", "python": platform.python_version(),
                          "participants": len(receipt["users"]), "archive_seconds": round(elapsed, 4), "duplicate_seconds": round(retry_elapsed, 4),
                          "duplicate": duplicate["duplicate"], "request_bytes": len(json.dumps(body).encode()), "response_bytes": len(json.dumps(receipt).encode()),
                          "database_bytes": path.stat().st_size, "outbox": store.outbox_status()}, indent=2))


if __name__ == "__main__":
    main()
