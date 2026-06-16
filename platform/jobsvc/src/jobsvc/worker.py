"""Job worker.

A small async loop that:

  1. Reclaims any Running jobs whose lease has expired (back to Queued).
  2. Claims one Queued job (FOR UPDATE SKIP LOCKED on Postgres).
  3. Recompiles the source (so we don't have to keep the compiled
     artifact across the queue boundary), submits via `providers.submit_to_provider`,
     stores the histogram, and transitions the job to Completed/Failed.

This module is tested directly from `tests/integration/test_worker.py`
without spawning a subprocess, by calling `process_one(session)` once
per claimed job.
"""

from __future__ import annotations

import asyncio
import os
import socket
import uuid
from datetime import datetime, timezone

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from . import audit
from .config import get_settings
from .db import session_scope
from .engine import compile_program
from .logging import get_logger
from .metrics import (
    ERRORS_TOTAL,
    JOBS_TOTAL,
    JOB_DURATION_SECONDS,
    QUEUE_DEPTH,
    WORKER_LEASE_EXPIRATIONS_TOTAL,
)
from .models import Job, JobState, Result
from .providers import submit_to_provider
from .queue import claim, reclaim_expired
from .registry import get_chip


_log = get_logger("jobsvc.worker")


def _worker_id() -> str:
    return f"{socket.gethostname()}-{os.getpid()}"


async def update_queue_depth(session: AsyncSession) -> None:
    """Read Queued count for the gauge."""
    n = (
        await session.execute(
            select(Job).where(Job.state == JobState.Queued)
        )
    ).scalars().all()
    QUEUE_DEPTH.set(len(n))


async def process_one(session: AsyncSession, *, worker_id: str | None = None) -> bool:
    """Try to claim and process one Queued job. Returns True if a job
    was processed (success or failure), False if the queue was empty.
    """
    wid = worker_id or _worker_id()

    reclaimed = await reclaim_expired(session)
    if reclaimed:
        WORKER_LEASE_EXPIRATIONS_TOTAL.inc(reclaimed)
        _log.info("worker.reclaim", count=reclaimed)

    job = await claim(session, wid)
    if job is None:
        await update_queue_depth(session)
        return False

    job_id = job.id
    started = datetime.now(timezone.utc)
    _log.info("worker.claim", job_id=str(job_id), worker=wid,
              target=job.target, shots=job.shots)
    await audit.record(
        session, user_id=job.user_id, action="job.claimed",
        target_type="job", target_id=job_id, detail={"worker": wid},
    )
    await session.commit()

    chip = get_chip(job.target)
    if chip is None:
        await _fail(session, job_id, "our", f"unknown target {job.target!r}")
        return True

    er = compile_program(job.source, job.source_kind, job.target)
    if not er.ok:
        await _fail(session, job_id, "our", f"compile failed: {er.error}")
        return True

    outcome = submit_to_provider(
        engine=er, chip=chip, shots=job.shots,
        program_name=job.name or "default",
    )

    async with session.begin_nested():
        # Re-fetch the row (we committed above) to pick up the latest
        # claim_expires_at and avoid stale state.
        j = (
            await session.execute(select(Job).where(Job.id == job_id))
        ).scalar_one()

        if outcome.ok:
            j.transition(JobState.Completed)
            j.claim_expires_at = None
            session.add(
                Result(
                    job_id=j.id,
                    counts=outcome.counts,
                    shots=outcome.shots,
                    raw_provider_payload=outcome.raw_payload,
                )
            )
            JOBS_TOTAL.labels(state="Completed").inc()
            await audit.record(
                session, user_id=j.user_id, action="job.completed",
                target_type="job", target_id=j.id,
                detail={"shots": j.shots, "provider": j.provider},
            )
        else:
            j.transition(JobState.Failed, reason=outcome.error,
                         error_kind=outcome.error_kind)
            j.claim_expires_at = None
            JOBS_TOTAL.labels(state="Failed").inc()
            await audit.record(
                session, user_id=j.user_id, action="job.failed",
                target_type="job", target_id=j.id,
                detail={"error": outcome.error,
                        "error_kind": outcome.error_kind},
            )

    await session.commit()
    duration = (datetime.now(timezone.utc) - started).total_seconds()
    JOB_DURATION_SECONDS.labels(
        state="Completed" if outcome.ok else "Failed"
    ).observe(duration)
    return True


async def _fail(
    session: AsyncSession, job_id: uuid.UUID, kind: str, msg: str
) -> None:
    j = (await session.execute(select(Job).where(Job.id == job_id))).scalar_one()
    j.transition(JobState.Failed, reason=msg, error_kind=kind)
    j.claim_expires_at = None
    JOBS_TOTAL.labels(state="Failed").inc()
    ERRORS_TOTAL.labels(kind=kind).inc()
    await audit.record(
        session, user_id=j.user_id, action="job.failed",
        target_type="job", target_id=j.id,
        detail={"error": msg, "error_kind": kind},
    )
    await session.commit()


async def loop() -> None:  # pragma: no cover (long-running)
    settings = get_settings()
    wid = _worker_id()
    _log.info("worker.start", worker=wid)
    while True:
        async with session_scope() as session:
            did = await process_one(session, worker_id=wid)
        if not did:
            await asyncio.sleep(settings.worker_poll_interval_seconds)


def run() -> None:  # pragma: no cover
    asyncio.run(loop())


__all__ = ["loop", "process_one", "run", "update_queue_depth"]
