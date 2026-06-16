"""POST/GET/DELETE /api/v1/jobs — the heart of the service."""

from __future__ import annotations

import base64
import json
import uuid
from decimal import Decimal
from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, Query, Request, status
from sqlalchemy import desc, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from .. import audit, cost
from ..auth import AuthenticatedUser, current_user
from ..db import get_session
from ..engine import compile_program
from ..models import Budget, Job, JobState, Result, UserRole
from ..registry import get_chip
from ..schemas import (
    EstimateOut,
    HistogramOut,
    JobCreate,
    JobDetail,
    JobList,
    JobOut,
)


router = APIRouter(prefix="/api/v1", tags=["jobs"])


def _job_out(j: Job) -> JobOut:
    return JobOut(
        id=j.id,
        user_id=j.user_id,
        name=j.name,
        target=j.target,
        shots=j.shots,
        source_kind=j.source_kind,
        state=j.state,
        rejection_reason=j.rejection_reason,
        error_kind=j.error_kind,
        estimate=EstimateOut(**j.estimate) if j.estimate else None,
        dollar_cost=j.dollar_cost,
        provider=j.provider,
        created_at=j.created_at,
        queued_at=j.queued_at,
        started_at=j.started_at,
        finished_at=j.finished_at,
    )


def _encode_cursor(created_at, ident: uuid.UUID) -> str:
    raw = json.dumps([created_at.isoformat(), str(ident)])
    return base64.urlsafe_b64encode(raw.encode()).decode()


def _decode_cursor(c: str) -> tuple[str, uuid.UUID]:
    raw = base64.urlsafe_b64decode(c.encode()).decode()
    ts, ident = json.loads(raw)
    return ts, uuid.UUID(ident)


@router.post(
    "/jobs",
    response_model=JobOut,
    status_code=status.HTTP_201_CREATED,
    responses={
        400: {"description": "compile error or unknown target"},
        402: {"description": "estimated cost exceeds user's budget"},
    },
)
async def create_job(
    body: JobCreate,
    request: Request,
    session: Annotated[AsyncSession, Depends(get_session)],
    user: Annotated[AuthenticatedUser, Depends(current_user)],
) -> JobOut:
    chip = get_chip(body.target)
    if chip is None:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"unknown target: {body.target}",
        )

    er = compile_program(body.source, body.source_kind, body.target)
    if not er.ok:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail={"reason": "compile_failed", "error": er.error},
        )

    job = Job(
        user_id=user.id,
        name=body.name,
        target=chip.id,
        shots=body.shots,
        source=body.source,
        source_kind=body.source_kind,
        estimate=er.estimate.to_dict(),
        dollar_cost=cost.dollar_cost(body.shots, chip),
        provider=chip.provider,
    )
    session.add(job)
    await session.flush()

    budget = (
        await session.execute(
            select(Budget).where(Budget.user_id == user.id)
        )
    ).scalar_one_or_none()

    spend = await cost.recent_spend_for_user(session, user.id)

    decision = cost.check_budget(
        budget=budget,
        shots=body.shots,
        chip=chip,
        recent_daily_spend=spend.daily,
        recent_monthly_spend=spend.monthly,
    )

    if decision.allow:
        job.transition(JobState.Queued)
        await audit.record(
            session,
            user_id=user.id,
            action="job.created",
            target_type="job",
            target_id=job.id,
            detail={"target": chip.id, "shots": body.shots,
                    "dollar_cost": str(job.dollar_cost or "0")},
            ip=request.client.host if request.client else None,
            ua=request.headers.get("user-agent"),
        )
        await session.commit()
        # Fire LISTEN/NOTIFY to wake workers (best-effort; the worker
        # will still poll on its own).
        await _notify_new_job(session, job.id)
        return _job_out(job)

    job.transition(JobState.Rejected, reason=decision.reason)
    await audit.record(
        session,
        user_id=user.id,
        action="job.rejected",
        target_type="job",
        target_id=job.id,
        detail=decision.detail,
    )
    await session.commit()
    raise HTTPException(
        status_code=status.HTTP_402_PAYMENT_REQUIRED,
        detail={
            "reason": decision.reason,
            **decision.detail,
        },
    )


async def _notify_new_job(session: AsyncSession, job_id: uuid.UUID) -> None:
    """Best-effort Postgres NOTIFY. SQLite tests skip this silently."""
    try:
        bind = session.bind
        if bind is None:
            return
        if "postgresql" not in str(bind.url):
            return
        await session.execute(
            __import__("sqlalchemy").text(f"NOTIFY jobs_new, '{job_id}'")
        )
    except Exception:
        pass


@router.get("/jobs/{job_id}", response_model=JobDetail, responses={404: {}})
async def get_job(
    job_id: uuid.UUID,
    session: Annotated[AsyncSession, Depends(get_session)],
    user: Annotated[AuthenticatedUser, Depends(current_user)],
) -> JobDetail:
    j = (
        await session.execute(
            select(Job)
            .where(Job.id == job_id)
            .options(selectinload(Job.result))
        )
    ).scalar_one_or_none()
    if j is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND)
    if j.user_id != user.id and not user.is_admin:
        # Pretend it doesn't exist (don't leak existence).
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND)

    payload = _job_out(j).model_dump()
    payload["source"] = j.source
    payload["result"] = (
        HistogramOut(counts=j.result.counts, shots=j.result.shots).model_dump()
        if j.result is not None
        else None
    )
    return JobDetail(**payload)


@router.get("/jobs", response_model=JobList)
async def list_jobs(
    session: Annotated[AsyncSession, Depends(get_session)],
    user: Annotated[AuthenticatedUser, Depends(current_user)],
    state: JobState | None = Query(default=None),
    limit: int = Query(default=20, ge=1, le=100),
    cursor: str | None = Query(default=None),
    user_id: uuid.UUID | None = Query(default=None),
) -> JobList:
    stmt = select(Job).order_by(desc(Job.created_at), desc(Job.id))
    if user.is_admin:
        if user_id:
            stmt = stmt.where(Job.user_id == user_id)
    else:
        stmt = stmt.where(Job.user_id == user.id)
    if state is not None:
        stmt = stmt.where(Job.state == state)
    if cursor:
        ts, ident = _decode_cursor(cursor)
        from datetime import datetime
        ts_dt = datetime.fromisoformat(ts)
        stmt = stmt.where(
            (Job.created_at < ts_dt)
            | ((Job.created_at == ts_dt) & (Job.id < ident))
        )
    stmt = stmt.limit(limit + 1)
    rows = (await session.execute(stmt)).scalars().all()
    has_more = len(rows) > limit
    rows = rows[:limit]
    next_cursor = (
        _encode_cursor(rows[-1].created_at, rows[-1].id) if has_more else None
    )
    return JobList(items=[_job_out(j) for j in rows], next_cursor=next_cursor)


@router.delete(
    "/jobs/{job_id}",
    status_code=status.HTTP_204_NO_CONTENT,
    responses={404: {}, 409: {"description": "already running or terminal"}},
)
async def cancel_job(
    job_id: uuid.UUID,
    session: Annotated[AsyncSession, Depends(get_session)],
    user: Annotated[AuthenticatedUser, Depends(current_user)],
) -> None:
    j = (
        await session.execute(select(Job).where(Job.id == job_id))
    ).scalar_one_or_none()
    if j is None or (j.user_id != user.id and not user.is_admin):
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND)
    if j.state not in (JobState.Submitted, JobState.Queued):
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=f"cannot cancel a job in state {j.state.value}",
        )
    j.transition(JobState.Rejected, reason="cancelled by user")
    await audit.record(
        session, user_id=user.id, action="job.cancelled",
        target_type="job", target_id=job_id,
    )
    await session.commit()
