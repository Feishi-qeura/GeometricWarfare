# Local verification

This service has not been deployed to Tencent/IIS and has not called Douyin with a real credential. Tests use synthetic identities and injected public-contract responses.

The tested local runtime is Python 3.12.14 / SQLite 3.53.1 / FastAPI 0.142.2 / uvicorn 0.54.0. Runtime versions are locked in `requirements.lock.txt`; HTTPX is a pinned test-only dependency. On this machine Windows sandbox restrictions hang the thread/ASGI loop, while the same suite under the normal permitted process context completes. This is why verification was run outside that sandbox, with no real token or external platform call.

The complete unittest suite passed **37 tests, 0 failures/errors**. It covers HTTP authentication/TLS/body bounds, exact Int64 room/score parsing, public four-operation wire bodies, sessions, duplicate/conflicting/parallel round receipts, atomic rollback, receiving-week cutoff/late policy, future-week rejection, post-lock timestamp selection, gray/win/streak behavior, ordered/fenced retries, completion configuration blocking, Int64 overflow isolation, instance locking and WAL-aware backup/restore. The log is in the project's ignored `Saved/Backend-Final6-Tests.log`.

`smoke_http.py` starts and stops a real loopback uvicorn service with `/live-api` root path. It verified local health, plaintext auth rejection (403) and trusted loopback HTTPS-forwarded but unauthenticated rejection (401). Zero live platform calls were made. Result: `Saved/Backend-Native-HTTP-Smoke.json`.

`benchmark.py` measures one actual local 5000-participant SQLite receipt and duplicate transaction, with fake identity and no publication network calls: archive **0.1629 s**, duplicate **0.0275 s**, request 396509 bytes, response 320171 bytes, database 3002368 bytes, 102 pending jobs. Result: `Saved/Backend-5000-Benchmark.json`. This is a local workstation measurement, not a Tencent 2-core/4-GB capacity guarantee. A separate maximum-ASCII 5000-user HTTP test validates a request exceeding 2 MiB but below 4 MiB, and a receipt below the host's 2 MiB limit.

Independent review reproduced and closed the future-version activation and first-transaction period-selection issues; late policy follows the user's explicit first-server-receipt decision. Completion timestamp unit, live auth, platform scope, world acceptance, IIS/ARR setup, server process supervision and target-host workload remain deployment checks described in README.md.
