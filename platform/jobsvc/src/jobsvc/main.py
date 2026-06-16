"""FastAPI app entry point.

Run from a shell:

    uvicorn jobsvc.main:app --host 0.0.0.0 --port 8000

Or programmatically:

    from jobsvc.main import run; run()
"""

from __future__ import annotations

import contextvars
import time
import uuid

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse

from . import __version__
from . import metrics as _metrics  # noqa: F401  -- registers Prometheus collectors
from .config import get_settings
from .logging import configure as configure_logging, get_logger
from .routers import health, jobs, login, targets, users


_request_id_var: contextvars.ContextVar[str] = contextvars.ContextVar(
    "request_id", default=""
)


def create_app() -> FastAPI:
    configure_logging()
    settings = get_settings()
    log = get_logger()

    app = FastAPI(
        title="Heisenberg quantum-stack — job service",
        version=__version__,
        description=(
            "FastAPI surface for the Photon/Phonon/Spinor compiler engine. "
            "Submits to providers in verbatim mode only (Rule 5)."
        ),
        docs_url="/api/docs",
        openapi_url="/api/openapi.json",
        redoc_url=None,
    )

    if settings.cors_origins:
        app.add_middleware(
            CORSMiddleware,
            allow_origins=settings.cors_origins,
            allow_methods=["*"],
            allow_headers=["*"],
            allow_credentials=True,
        )

    @app.middleware("http")
    async def _request_context(request: Request, call_next):
        rid = request.headers.get("x-request-id") or uuid.uuid4().hex
        token = _request_id_var.set(rid)
        t0 = time.monotonic()
        log.info(
            "request.start",
            method=request.method,
            path=request.url.path,
            request_id=rid,
        )
        try:
            response = await call_next(request)
        except Exception as exc:
            log.exception(
                "request.error",
                method=request.method,
                path=request.url.path,
                request_id=rid,
                error=str(exc),
            )
            return JSONResponse(
                {"detail": "internal error", "request_id": rid},
                status_code=500,
            )
        finally:
            _request_id_var.reset(token)
        response.headers["x-request-id"] = rid
        log.info(
            "request.end",
            method=request.method,
            path=request.url.path,
            status=response.status_code,
            duration_ms=int((time.monotonic() - t0) * 1000),
            request_id=rid,
        )
        return response

    app.include_router(health.router)
    app.include_router(login.router)
    app.include_router(targets.router)
    app.include_router(users.router)
    app.include_router(jobs.router)
    return app


app = create_app()


def run() -> None:  # pragma: no cover
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=8000)
