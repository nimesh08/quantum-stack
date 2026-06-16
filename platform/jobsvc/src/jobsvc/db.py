"""Async SQLAlchemy 2.0 engine + session factory.

Single-file: an engine cached per database URL, and a `get_session`
FastAPI dependency. Tests can override the cache by calling
`reset_engine()` in fixtures.
"""

from __future__ import annotations

from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from functools import lru_cache
from typing import Any

from sqlalchemy.ext.asyncio import (
    AsyncEngine,
    AsyncSession,
    async_sessionmaker,
    create_async_engine,
)
from sqlalchemy.orm import DeclarativeBase

from .config import get_settings


class Base(DeclarativeBase):
    """Single declarative base shared by every ORM model."""

    type_annotation_map: dict[type, Any] = {}


@lru_cache(maxsize=8)
def get_engine(url: str | None = None, *, echo: bool = False) -> AsyncEngine:
    settings = get_settings()
    return create_async_engine(
        url or settings.database_url,
        echo=echo or settings.sql_echo,
        pool_pre_ping=True,
        future=True,
    )


def get_sessionmaker(
    engine: AsyncEngine | None = None,
) -> async_sessionmaker[AsyncSession]:
    return async_sessionmaker(
        engine or get_engine(),
        expire_on_commit=False,
        class_=AsyncSession,
    )


def reset_engine() -> None:
    """Used by tests to drop cached engines between runs."""
    get_engine.cache_clear()


@asynccontextmanager
async def session_scope() -> AsyncIterator[AsyncSession]:
    """Standalone context manager — workers + scripts use this."""
    sm = get_sessionmaker()
    async with sm() as session:
        try:
            yield session
            await session.commit()
        except Exception:
            await session.rollback()
            raise


async def get_session() -> AsyncIterator[AsyncSession]:
    """FastAPI dependency."""
    sm = get_sessionmaker()
    async with sm() as session:
        try:
            yield session
            await session.commit()
        except Exception:
            await session.rollback()
            raise


__all__ = [
    "Base",
    "get_engine",
    "get_session",
    "get_sessionmaker",
    "reset_engine",
    "session_scope",
]
