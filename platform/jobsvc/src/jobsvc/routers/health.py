"""GET /healthz, /readyz, /metrics."""

from __future__ import annotations

from typing import Annotated

from fastapi import APIRouter, Depends, Response
from prometheus_client import CONTENT_TYPE_LATEST, generate_latest
from sqlalchemy import text
from sqlalchemy.ext.asyncio import AsyncSession

from .. import __version__
from ..db import get_session
from ..engine import engine_available
from ..schemas import HealthOut


router = APIRouter(tags=["health"])


@router.get("/healthz", response_model=HealthOut)
async def healthz() -> HealthOut:
    return HealthOut(
        status="ok",
        version=__version__,
        engine_available=engine_available(),
    )


@router.get("/readyz", response_model=HealthOut)
async def readyz(
    session: Annotated[AsyncSession, Depends(get_session)],
) -> HealthOut:
    await session.execute(text("SELECT 1"))
    return HealthOut(
        status="ready",
        version=__version__,
        engine_available=engine_available(),
    )


@router.get("/metrics")
async def metrics() -> Response:
    return Response(content=generate_latest(), media_type=CONTENT_TYPE_LATEST)
