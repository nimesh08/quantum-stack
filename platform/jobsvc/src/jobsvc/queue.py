"""Postgres-as-queue helpers.

Implements the contract described in
[`phaseD_decisions.md`](../../docs/build/phaseD_decisions.md) D1:

- [`enqueue`][jobsvc.queue.enqueue] — fires
  ``NOTIFY jobs_new, '<job_id>'`` inside the same transaction as the
  row update. SQLite tests silently skip the NOTIFY.
- [`claim`][jobsvc.queue.claim] —
  ``SELECT ... FOR UPDATE SKIP LOCKED`` plus a state transition to
  ``Running`` with ``claimed_by`` and ``claim_expires_at``. SQLite
  path uses a plain UPDATE (single-writer only — fine for tests).
- [`reclaim_expired`][jobsvc.queue.reclaim_expired] — sweeps Running
  jobs whose lease has expired back to Queued so another worker can
  pick them up.
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
    """Wake up workers via Postgres ``LISTEN/NOTIFY``.

    Best-effort: when the engine isn't Postgres (SQLite tests), this
    is a no-op. The worker poll loop still picks the job up on its
    next tick.

    Args:
        session: Async SQLAlchemy session. Must be the same session
            that just inserted the ``Queued`` row, so the NOTIFY
            ships in the same transaction.
        job_id: Newly-queued job's id (becomes the NOTIFY payload).

    Example:
        >>> # await enqueue(session, job.id)
        True
        True
    """
    if _is_postgres(session):
        await session.execute(text(f"NOTIFY jobs_new, '{job_id}'"))


async def claim(
    session: AsyncSession,
    worker_id: str,
    *,
    lease_seconds: int | None = None,
) -> Optional[Job]:
    """Atomically transition the oldest Queued job to Running.

    On Postgres, uses ``SELECT ... FOR UPDATE SKIP LOCKED`` so two
    workers running in parallel never claim the same job. On SQLite
    (single-writer; tests only) the same query without the lock works
    because there is exactly one writer.

    Args:
        session: Async SQLAlchemy session.
        worker_id: Stable identifier for this worker process; written
            to ``Job.claimed_by`` so reclaim and observability know
            who held the lease. Convention:
            ``"<hostname>-<pid>"``.
        lease_seconds: How long this worker may hold the job before
            another may steal it. Defaults to
            ``settings.worker_lease_seconds`` (300 seconds).

    Returns:
        The claimed [`Job`][jobsvc.models.Job], or ``None`` if the
        queue is empty.

    Example:
        >>> # job = await claim(session, "host-1234")
        >>> # if job is not None: ...
        True
        True
    """
    settings = get_settings()
    lease = lease_seconds or settings.worker_lease_seconds
    expires_at = _now() + timedelta(seconds=lease)

    if _is_postgres(session):
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
    """Push Running jobs whose lease expired back to Queued.

    Called once at the top of every
    [`worker.process_one`][jobsvc.worker.process_one] cycle so a
    worker that crashes mid-job doesn't leave its work stranded.

    Args:
        session: Async SQLAlchemy session.

    Returns:
        Number of jobs re-queued. 0 in the common case.

    Example:
        >>> # n = await reclaim_expired(session)
        >>> # if n: log.warning("reclaimed", count=n)
        True
        True
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
