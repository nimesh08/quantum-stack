"""FastAPI app entry point.

Run from a shell:

    uvicorn jobsvc.main:app --host 0.0.0.0 --port 8080

Or programmatically:

    from jobsvc.main import run; run()

The launcher (`heisenberg run`) uses `uvicorn.Server` directly so it
can supervise the worker + scheduler in the same process.

Author: Nimesh Cheedella <chnimesh0808@gmail.com>
"""

from __future__ import annotations

import contextvars
import time
import uuid
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from starlette.exceptions import HTTPException as StarletteHTTPException

from . import __version__
from . import metrics as _metrics  # noqa: F401  -- registers Prometheus collectors
from .config import get_settings
from .logging import configure as configure_logging, get_logger
from .routers import health, jobs, login, targets, users


_request_id_var: contextvars.ContextVar[str] = contextvars.ContextVar(
    "request_id", default=""
)


def _static_dir() -> Path:
    """Where the built playground SPA lives, if it has been built."""
    return Path(__file__).resolve().parent / "static"


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

    static = _static_dir()
    if static.is_dir() and (static / "index.html").exists():
        # SPA: serve `static/` at `/`, but make sure `/api/*` and the
        # OpenAPI/docs surfaces still take precedence (they were
        # registered above via include_router). On a 404 inside an
        # `/`-mounted SPA we want `index.html` so client-side routing
        # works on hard refresh.
        assets = static / "assets"
        if assets.is_dir():
            app.mount(
                "/assets",
                StaticFiles(directory=assets),
                name="spa-assets",
            )

        @app.get("/", include_in_schema=False)
        async def _spa_root() -> "JSONResponse":  # type: ignore[name-defined]
            from fastapi.responses import FileResponse

            return FileResponse(static / "index.html")

        @app.exception_handler(StarletteHTTPException)
        async def _spa_fallback(
            request: Request, exc: StarletteHTTPException
        ):
            from fastapi.responses import FileResponse

            if (
                exc.status_code == 404
                and not request.url.path.startswith("/api/")
                and not request.url.path.startswith("/healthz")
                and not request.url.path.startswith("/readyz")
                and not request.url.path.startswith("/metrics")
                and (static / "index.html").exists()
            ):
                return FileResponse(static / "index.html")
            return JSONResponse(
                {"detail": exc.detail}, status_code=exc.status_code
            )

    return app


app = create_app()


def run() -> None:  # pragma: no cover
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=8080)
