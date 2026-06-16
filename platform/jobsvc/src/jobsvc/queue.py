"""Postgres-as-queue helpers.

Implements the contract described in `phaseD_decisions.md` D1:

  - `enqueue(session, job_id)` — fires `NOTIFY jobs_new, '<job_id>'`
    inside the same transaction as the row update. SQLite tests
    silently skip the NOTIFY.
  - `claim(session, worker_id)` — `SELECT ... FOR UPDATE SKIP LOCKED`
    plus a state transition to Running with `claimed_by` and
    `claim_expires_at`. SQLite path uses a plain UPDATE (single-writer
    only — fine for tests).
  - `reclaim_expired(session)` — sweeps Running jobs whose lease has
    expired back to Queued so another worker can pick them up.
"""

from __future__ import annotations

import uuid
from datetime import datetime, timedelta, timezone
from typing import Optional

from sqlalchemy import and_, or_, select, text, update
from sqlalchemy.ext.asyncio import AsyncSession

from .config import get_settings
from .models import Job, JobState


def _now() -> datetime:
    return datetime.now(timezone.utc)


def _is_postgres(session: AsyncSession) -> bool:
    bind = session.get_bind()
    if bind is None:
        return False
    return "postgresql" in str(bind.url)


async def enqueue(session: AsyncSession, job_id: uuid.UUID) -> None:
    if _is_postgres(session):
        await session.execute(text(f"NOTIFY jobs_new, '{job_id}'"))


async def claim(
    session: AsyncSession,
    worker_id: str,
    *,
    lease_seconds: int | None = None,
) -> Optional[Job]:
    """Atomically transition the oldest Queued job to Running.

    Returns the claimed Job or None if the queue is empty.
    """
    settings = get_settings()
    lease = lease_seconds or settings.worker_lease_seconds
    expires_at = _now() + timedelta(seconds=lease)

    if _is_postgres(session):
        # SELECT id ... FOR UPDATE SKIP LOCKED, then UPDATE that id.
        row = (
            await session.execute(
                select(Job)
                .where(Job.state == JobState.Queued)
                .order_by(Job.queued_at, Job.id)
                .with_for_update(skip_locked=True)
                .limit(1)
            )
        ).scalars().first()
    else:
        row = (
            await session.execute(
                select(Job)
                .where(Job.state == JobState.Queued)
                .order_by(Job.queued_at, Job.id)
                .limit(1)
            )
        ).scalars().first()

    if row is None:
        return None

    row.transition(JobState.Running)
    row.claimed_by = worker_id
    row.claim_expires_at = expires_at
    await session.flush()
    return row


async def reclaim_expired(session: AsyncSession) -> int:
    """Push Running jobs whose lease expired back to Queued. Returns
    the number of jobs reclaimed.
    """
    now = _now()
    rows = (
        await session.execute(
            select(Job).where(
                and_(
                    Job.state == JobState.Running,
                    Job.claim_expires_at.is_not(None),
                    Job.claim_expires_at < now,
                )
            )
        )
    ).scalars().all()
    n = 0
    for r in rows:
        r.transition(JobState.Queued)
        r.claimed_by = None
        r.claim_expires_at = None
        n += 1
    return n


__all__ = ["claim", "enqueue", "reclaim_expired"]
