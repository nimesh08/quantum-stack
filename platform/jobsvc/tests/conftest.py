"""Pytest fixtures shared across unit + integration."""

from __future__ import annotations

import os
from collections.abc import AsyncIterator
from dataclasses import dataclass
from typing import Any

import pytest
import pytest_asyncio
from httpx import ASGITransport, AsyncClient
from sqlalchemy.ext.asyncio import (
    AsyncEngine,
    AsyncSession,
    async_sessionmaker,
    create_async_engine,
)

from jobsvc.auth.jwt import create_token_pair
from jobsvc.auth.passwords import hash_password
from jobsvc.db import Base, get_session
from jobsvc.main import create_app
from jobsvc.models import Budget, User, UserRole


@pytest_asyncio.fixture
async def sqlite_engine() -> AsyncIterator[AsyncEngine]:
    """Per-test in-memory SQLite engine. Handles GUID + JSON via the
    cross-dialect helpers in `jobsvc.models`.
    """
    engine = create_async_engine(
        "sqlite+aiosqlite:///:memory:", future=True
    )
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    try:
        yield engine
    finally:
        async with engine.begin() as conn:
            await conn.run_sync(Base.metadata.drop_all)
        await engine.dispose()


@pytest_asyncio.fixture
async def session(sqlite_engine: AsyncEngine) -> AsyncIterator[AsyncSession]:
    sm = async_sessionmaker(sqlite_engine, expire_on_commit=False)
    async with sm() as s:
        yield s


# ---- Postgres integration fixtures (skipped if no DB available) ----


def _have_postgres() -> bool:
    return bool(os.environ.get("JOBSVC_DATABASE_URL", "").startswith("postgresql"))


@pytest.fixture(scope="session")
def postgres_url() -> str:
    url = os.environ.get("JOBSVC_DATABASE_URL", "")
    if not url:
        pytest.skip("JOBSVC_DATABASE_URL not set; integration tests skipped")
    return url


# ---------- App-level fixtures (httpx + seeded users) ----------


@dataclass(slots=True)
class SeededUser:
    id: Any
    email: str
    role: UserRole
    password: str
    access_token: str
    refresh_token: str


def _auth(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


@pytest_asyncio.fixture
async def client_env() -> AsyncIterator[dict[str, Any]]:
    engine: AsyncEngine = create_async_engine(
        "sqlite+aiosqlite:///:memory:", future=True
    )
    sm = async_sessionmaker(engine, expire_on_commit=False)

    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)

    async def _override_get_session() -> AsyncIterator[AsyncSession]:
        async with sm() as s:
            try:
                yield s
                await s.commit()
            except Exception:
                await s.rollback()
                raise

    app = create_app()
    app.dependency_overrides[get_session] = _override_get_session

    async with sm() as s:
        u = User(
            email="user@x.com",
            password_hash=hash_password("user-password"),
            role=UserRole.user,
        )
        s.add(u)
        await s.flush()
        s.add(Budget(user_id=u.id))

        a = User(
            email="admin@x.com",
            password_hash=hash_password("admin-password"),
            role=UserRole.admin,
        )
        s.add(a)
        await s.flush()
        s.add(Budget(user_id=a.id))
        await s.commit()
        await s.refresh(u)
        await s.refresh(a)

    u_access, u_refresh, _ = create_token_pair(u.id, u.role.value)
    a_access, a_refresh, _ = create_token_pair(a.id, a.role.value)

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://t") as client:
        yield {
            "app": app,
            "client": client,
            "sm": sm,
            "user": SeededUser(
                id=u.id, email=u.email, role=u.role,
                password="user-password",
                access_token=u_access, refresh_token=u_refresh,
            ),
            "admin": SeededUser(
                id=a.id, email=a.email, role=a.role,
                password="admin-password",
                access_token=a_access, refresh_token=a_refresh,
            ),
            "auth": _auth,
        }

    await engine.dispose()
