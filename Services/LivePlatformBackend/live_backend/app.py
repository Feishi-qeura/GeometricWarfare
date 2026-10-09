"""Thin bounded HTTP adapter. Request credentials and bodies are never logged."""
import ipaddress
import json
import logging
import sqlite3
import threading
from contextlib import asynccontextmanager
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from starlette.concurrency import run_in_threadpool

from .service import Backend, Settings, RoundPending
from .store import Store, Conflict, Unauthorized
from .upstream import DouyinAPI, UpstreamError
from .worker import Worker
from .instance import InstanceLock

BODY_LIMIT = 4 * 1024 * 1024
LOG = logging.getLogger("live_backend")


def bearer(request):
    header = request.headers.get("authorization", "")
    parts = header.split(" ")
    if len(parts) != 2 or parts[0].lower() != "bearer" or not 1 <= len(parts[1]) <= 8192:
        raise Unauthorized("authorization_required")
    return parts[1]


async def bounded_json(request):
    if request.headers.get("content-type", "").split(";", 1)[0].lower() != "application/json":
        raise ValueError("json_required")
    content = bytearray()
    async for chunk in request.stream():
        if len(content) + len(chunk) > BODY_LIMIT:
            raise OverflowError("body_limit")
        content.extend(chunk)
    def unique_fields(pairs):
        value = {}
        for key, entry in pairs:
            if key in value:
                raise ValueError("duplicate_json_field")
            value[key] = entry
        return value
    value = json.loads(content, object_pairs_hook=unique_fields)
    if not isinstance(value, dict):
        raise ValueError("object_required")
    return value


def create_app(*, backend=None, start_worker=True):
    if backend is None:
        settings = Settings.from_env()
        backend = Backend(settings, Store(settings.database_path), DouyinAPI(settings.app_id, settings.app_secret))
    settings = backend.settings
    stopped = threading.Event()
    worker = Worker(backend)

    def delivery_loop():
        while not stopped.is_set():
            try:
                if settings.app_id:
                    backend.rollover()
                active = worker.run_once()
            except Exception:
                # No exception repr/traceback: upstream content and auth never
                # become log messages, including third-party failure paths.
                LOG.error("backend worker failure; inspect operator status")
                active = False
            stopped.wait(.1 if active else 1)

    @asynccontextmanager
    async def lifespan(app):
        thread = None
        instance = None
        if start_worker:
            instance = InstanceLock(str(settings.database_path) + ".lock").acquire()
            app.state.worker_instance_lock = instance
            thread = threading.Thread(target=delivery_loop, name="world-outbox", daemon=True)
            thread.start()
        yield
        stopped.set()
        if thread:
            # A crash still leaves durable leases recoverable. Normal shutdown
            # lets the bounded two-request token+publish handoff finish first.
            await run_in_threadpool(thread.join, 35)
        if instance and not thread.is_alive():
            instance.release()

    app = FastAPI(title="Live Platform Backend", docs_url=None, redoc_url=None, openapi_url=None, lifespan=lifespan)
    app.state.backend = backend

    @app.middleware("http")
    async def transport_guard(request, call_next):
        try:
            loopback = request.client and ipaddress.ip_address(request.client.host).is_loopback
        except ValueError:
            loopback = False
        health = request.url.path.endswith("/healthz") and request.method == "GET"
        if request.url.scheme != "https" and not (loopback and (settings.allow_loopback_http or health)):
            return JSONResponse({"error": "https_required"}, status_code=403)
        length = request.headers.get("content-length")
        if length is not None:
            try:
                size = int(length)
                if size < 0 or size > BODY_LIMIT:
                    raise ValueError()
            except ValueError:
                return JSONResponse({"error": "body_limit"}, status_code=413)
        response = await call_next(request)
        response.headers["Cache-Control"] = "no-store"
        return response

    @app.get("/healthz")
    def health():
        status = backend.repository.outbox_status()
        configured = bool(settings.app_id and settings.app_secret)
        return {"status": "ready" if configured else "configuration_required", "uploads_enabled": settings.enable_uploads,
                "complete_time_unit": settings.complete_time_unit or "pending_verification", "outbox": status,
                "publication_state": "blocked_failed_job" if status.get("failed") else ("completion_configuration_required" if not settings.complete_time_unit else "queued_or_current")}

    async def invoke(request, operation):
        try:
            credential = bearer(request)
            body = await bounded_json(request)
            return await run_in_threadpool(operation, credential, body)
        except Unauthorized:
            return JSONResponse({"error": "unauthorized_identity_or_session"}, status_code=401)
        except RoundPending:
            return JSONResponse({"error": "round_time_pending"}, status_code=503)
        except Conflict:
            return JSONResponse({"error": "round_conflict"}, status_code=409)
        except OverflowError:
            return JSONResponse({"error": "body_limit"}, status_code=413)
        except (ValueError, UnicodeError):
            return JSONResponse({"error": "invalid_request"}, status_code=422)
        except UpstreamError as error:
            return JSONResponse({"error": "upstream_unavailable" if error.transient else "platform_authentication_rejected"}, status_code=503 if error.transient else 401)
        except sqlite3.Error:
            return JSONResponse({"error": "storage_unavailable"}, status_code=503)
        except Exception:
            LOG.error("backend request failure; no request data logged")
            return JSONResponse({"error": "service_unavailable"}, status_code=503)

    @app.post("/v1/douyin/sessions")
    async def sessions(request: Request):
        return await invoke(request, backend.exchange)

    @app.post("/v1/douyin/rounds")
    async def rounds(request: Request):
        return await invoke(request, backend.submit)

    return app
