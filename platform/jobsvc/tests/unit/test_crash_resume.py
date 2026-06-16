"""Crash-and-resume regression at the model level.

Simulates: a worker claims a job, dies; lease expires; another worker
re-queues and runs to completion. Asserts the audit log captures the
reclaim.
"""

from __future__ import annotations

import uuid
from datetime import datetime, timedelta, timezone

import pytest
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from jobsvc.audit import record
from jobsvc.models import AuditLog, Job, JobState, SourceKind, User


@pytest.mark.asyncio
async def test_crash_and_resume(session: AsyncSession) -> None:
    u = User(email="crash@e.com", password_hash="x")
    session.add(u)
    await session.flush()
    j = Job(
        user_id=u.id,
        target="generic",
        shots=10,
        source="x",
        source_kind=SourceKind.spinor,
    )
    session.add(j)
    await session.flush()

    j.transition(JobState.Queued)
    await record(
        session, user_id=u.id, action="job.queued", target_id=j.id
    )

    j.transition(JobState.Running)
    j.claimed_by = "worker-1"
    j.claim_expires_at = datetime.now(timezone.utc) - timedelta(seconds=1)
    await record(
        session,
        user_id=u.id,
        action="job.claimed",
        target_id=j.id,
        detail={"worker": "worker-1"},
    )
    await session.commit()

    # === simulated worker crash ===
    # The next claim cycle observes a Running job whose lease has
    # expired and re-queues it.
    loaded = (
        await session.execute(select(Job).where(Job.id == j.id))
    ).scalar_one()
    assert loaded.state is JobState.Running
    assert loaded.claim_expires_at < datetime.now(timezone.utc)

    loaded.transition(JobState.Queued)
    loaded.claimed_by = None
    loaded.claim_expires_at = None
    await record(
        session,
        user_id=u.id,
        action="job.reclaimed",
        target_id=j.id,
        detail={"reason": "lease expired"},
    )
    await session.commit()

    # Second worker picks it up.
    loaded.transition(JobState.Running)
    loaded.claimed_by = "worker-2"
    loaded.transition(JobState.Completed)
    await record(
        session, user_id=u.id, action="job.completed", target_id=j.id
    )
    await session.commit()

    final = (
        await session.execute(select(Job).where(Job.id == j.id))
    ).scalar_one()
    assert final.state is JobState.Completed

    actions = [
        r.action
        for r in (
            await session.execute(
                select(AuditLog).order_by(AuditLog.at)
            )
        ).scalars()
    ]
    assert actions == [
        "job.queued",
        "job.claimed",
        "job.reclaimed",
        "job.completed",
    ]
