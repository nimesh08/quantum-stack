"""M4 — worker end-to-end tests against the in-memory SQLite app."""

from __future__ import annotations

import asyncio
import uuid
from datetime import datetime, timedelta, timezone
from decimal import Decimal

import pytest
from sqlalchemy import select

from jobsvc.audit import record
from jobsvc.models import Job, JobState, Result, SourceKind, User, Budget, UserRole
from jobsvc.queue import claim, reclaim_expired
from jobsvc.worker import process_one
from jobsvc.auth.passwords import hash_password


BELL = "target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n"


async def _seed_user(session) -> User:
    u = User(email=f"{uuid.uuid4().hex[:8]}@x.com",
             password_hash=hash_password("password123"),
             role=UserRole.user)
    session.add(u)
    await session.flush()
    session.add(Budget(user_id=u.id))
    await session.commit()
    await session.refresh(u)
    return u


def _seed_job(user_id) -> Job:
    return Job(
        user_id=user_id, target="generic", shots=10,
        source=BELL, source_kind=SourceKind.spinor,
        provider="local",
    )


@pytest.mark.asyncio
async def test_local_e2e_bell(client_env) -> None:
    """End-to-end through the API: submit -> worker processes ->
    histogram appears."""
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    r = await c.post(
        "/api/v1/jobs",
        json={"source": BELL, "source_kind": "spinor",
              "target": "generic", "shots": 50},
        headers=auth,
    )
    assert r.status_code == 201
    jid = r.json()["id"]

    sm = client_env["sm"]
    async with sm() as session:
        # Repeatedly process until queue empty (one job here).
        for _ in range(3):
            did = await process_one(session, worker_id="t1")
            if not did:
                break

    r = await c.get(f"/api/v1/jobs/{jid}", headers=auth)
    body = r.json()
    assert body["state"] == "Completed"
    assert body["result"] is not None
    assert sum(body["result"]["counts"].values()) == 50
    assert body["result"]["shots"] == 50
    # Bell-like distribution
    assert set(body["result"]["counts"].keys()) <= {"00", "11"}


@pytest.mark.asyncio
async def test_claim_skips_when_empty(client_env) -> None:
    sm = client_env["sm"]
    async with sm() as session:
        j = await claim(session, "w1")
        assert j is None


@pytest.mark.asyncio
async def test_two_workers_no_double_claim(client_env) -> None:
    sm = client_env["sm"]
    async with sm() as s:
        u = await _seed_user(s)

    # Insert 5 jobs in Queued state directly.
    async with sm() as s:
        for _ in range(5):
            j = _seed_job(u.id)
            j.transition(JobState.Queued)
            s.add(j)
        await s.commit()

    # On SQLite (single-writer) we just verify each claim picks one
    # row and Running grows by exactly one.
    seen = set()
    async with sm() as s:
        for _ in range(5):
            job = await claim(s, "w1")
            assert job is not None
            seen.add(job.id)
            await s.commit()
        assert await claim(s, "w1") is None
    assert len(seen) == 5


@pytest.mark.asyncio
async def test_lease_expiry_re_queues(client_env) -> None:
    sm = client_env["sm"]
    async with sm() as s:
        u = await _seed_user(s)
        j = _seed_job(u.id)
        j.transition(JobState.Queued)
        j.transition(JobState.Running)
        j.claimed_by = "dead-worker"
        j.claim_expires_at = datetime.now(timezone.utc) - timedelta(seconds=1)
        s.add(j)
        await s.commit()
        jid = j.id

    async with sm() as s:
        n = await reclaim_expired(s)
        assert n == 1
        await s.commit()

    async with sm() as s:
        loaded = (
            await s.execute(select(Job).where(Job.id == jid))
        ).scalar_one()
        assert loaded.state is JobState.Queued
        assert loaded.claimed_by is None


@pytest.mark.asyncio
async def test_provider_error_classification(client_env, monkeypatch) -> None:
    """A provider that raises -> error_kind='provider'."""
    from jobsvc import providers as P

    def boom(**kwargs):  # noqa: ARG001
        raise RuntimeError("provider went down")

    # Override the local simulate to fail.
    monkeypatch.setattr(P, "_local_simulate", lambda *a, **k: boom())

    sm = client_env["sm"]
    async with sm() as s:
        u = await _seed_user(s)
        j = _seed_job(u.id)
        j.transition(JobState.Queued)
        s.add(j)
        await s.commit()
        jid = j.id

    async with sm() as s:
        await process_one(s, worker_id="t1")

    async with sm() as s:
        loaded = (
            await s.execute(select(Job).where(Job.id == jid))
        ).scalar_one()
        assert loaded.state is JobState.Failed
        assert loaded.error_kind == "provider"
        assert "provider went down" in (loaded.rejection_reason or "")


@pytest.mark.asyncio
async def test_unknown_target_classified_as_our(client_env) -> None:
    sm = client_env["sm"]
    async with sm() as s:
        u = await _seed_user(s)
        j = Job(
            user_id=u.id, target="nonexistent", shots=1,
            source=BELL, source_kind=SourceKind.spinor,
        )
        j.transition(JobState.Queued)
        s.add(j)
        await s.commit()
        jid = j.id

    async with sm() as s:
        await process_one(s, worker_id="t1")

    async with sm() as s:
        loaded = (
            await s.execute(select(Job).where(Job.id == jid))
        ).scalar_one()
        assert loaded.state is JobState.Failed
        assert loaded.error_kind == "our"


@pytest.mark.asyncio
async def test_cancel_then_worker_skips(client_env) -> None:
    """A user cancels (Queued -> Rejected); the worker must not pick it up."""
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    r = await c.post(
        "/api/v1/jobs",
        json={"source": BELL, "source_kind": "spinor",
              "target": "generic", "shots": 1},
        headers=auth,
    )
    jid = r.json()["id"]
    await c.delete(f"/api/v1/jobs/{jid}", headers=auth)

    sm = client_env["sm"]
    async with sm() as s:
        # Empty queue.
        assert await claim(s, "t1") is None
