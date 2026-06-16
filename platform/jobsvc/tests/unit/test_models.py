"""Models smoke: ORM round-trip on SQLite."""

from __future__ import annotations

import uuid
from datetime import datetime, timezone
from decimal import Decimal

import pytest
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from jobsvc.audit import record
from jobsvc.models import (
    AuditLog,
    Budget,
    Job,
    JobState,
    Result,
    SourceKind,
    User,
)


@pytest.mark.asyncio
async def test_user_job_result_roundtrip(session: AsyncSession) -> None:
    u = User(email="alice@example.com", password_hash="bcrypt$xxx")
    session.add(u)
    await session.flush()

    j = Job(
        user_id=u.id,
        target="ibm_heron_r2",
        shots=1000,
        source="target generic\n",
        source_kind=SourceKind.spinor,
        estimate={"num_qubits": 2, "two_qubit_count": 1, "depth": 4, "t_count": 0},
        dollar_cost=Decimal("0.330000"),
    )
    session.add(j)
    await session.flush()

    j.transition(JobState.Queued)
    j.transition(JobState.Running)
    j.transition(JobState.Completed)
    session.add(
        Result(job_id=j.id, counts={"00": 498, "11": 502}, shots=1000)
    )
    await session.commit()

    loaded = (
        await session.execute(
            select(Job)
            .where(Job.id == j.id)
            .options(selectinload(Job.result))
        )
    ).scalar_one()
    assert loaded.state is JobState.Completed
    assert loaded.estimate["num_qubits"] == 2
    assert loaded.result is not None
    assert loaded.result.counts["00"] == 498


@pytest.mark.asyncio
async def test_budget_defaults(session: AsyncSession) -> None:
    u = User(email="bob@example.com", password_hash="x")
    session.add(u)
    await session.flush()
    b = Budget(user_id=u.id)
    session.add(b)
    await session.commit()

    loaded = (
        await session.execute(select(Budget).where(Budget.user_id == u.id))
    ).scalar_one()
    assert Decimal(str(loaded.daily_usd)) == Decimal("1.00")
    assert Decimal(str(loaded.monthly_usd)) == Decimal("10.00")
    assert loaded.max_shots_per_job == 10_000


@pytest.mark.asyncio
async def test_audit_record_is_persisted(session: AsyncSession) -> None:
    u = User(email="cathy@example.com", password_hash="x")
    session.add(u)
    await session.flush()

    await record(
        session,
        user_id=u.id,
        action="login",
        target_type="user",
        target_id=u.id,
        ip="127.0.0.1",
        ua="pytest",
        detail={"method": "password"},
    )
    await session.commit()

    rows = (await session.execute(select(AuditLog))).scalars().all()
    assert len(rows) == 1
    assert rows[0].action == "login"
    assert rows[0].detail == {"method": "password"}


@pytest.mark.asyncio
async def test_estimate_roundtrip_preserves_types(
    session: AsyncSession,
) -> None:
    u = User(email="d@e.f", password_hash="x")
    session.add(u)
    await session.flush()
    j = Job(
        user_id=u.id,
        target="generic",
        shots=1,
        source="x",
        source_kind=SourceKind.phonon,
        estimate={
            "num_qubits": 8,
            "two_qubit_count": 12,
            "depth": 24,
            "t_count": 4,
        },
    )
    session.add(j)
    await session.commit()

    loaded = (
        await session.execute(select(Job).where(Job.id == j.id))
    ).scalar_one()
    assert loaded.estimate["t_count"] == 4
