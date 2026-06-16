"""Cost control — M3, the first quantum-specific seam.

Compute the dollar cost of a job from its resource estimate and the
chip's `pricing.per_shot_usd`, then check it against the user's
`Budget` plus their recent spend window.

The seam is exercised from `routers.jobs.create_job` between
"compile succeeded" and "queue the job". The contract:

    decision = check_budget(budget=..., shots=..., chip=...,
                            recent_daily_spend=..., recent_monthly_spend=...)
    if decision.allow:
        ...transition to Queued...
    else:
        ...transition to Rejected, return 402 with decision.detail...

The recent-spend numbers come from
:func:`recent_spend_for_user`, which sums `dollar_cost` over jobs in
non-rejected states inside today's UTC day and month respectively.
"""

from __future__ import annotations

import uuid
from dataclasses import dataclass, field
from datetime import datetime, time, timedelta, timezone
from decimal import Decimal
from typing import Any

from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from .models import Budget, Job, JobState
from .registry import ChipInfo


@dataclass(slots=True)
class CostDecision:
    allow: bool
    reason: str = ""
    detail: dict[str, Any] = field(default_factory=dict)


@dataclass(slots=True)
class RecentSpend:
    daily: Decimal
    monthly: Decimal


def dollar_cost(shots: int, chip: ChipInfo) -> Decimal:
    if shots < 0:
        raise ValueError("shots must be >= 0")
    return (Decimal(shots) * chip.pricing_per_shot_usd).quantize(
        Decimal("0.000001")
    )


def _utc_day_start(now: datetime | None = None) -> datetime:
    n = now or datetime.now(timezone.utc)
    return datetime.combine(n.date(), time.min, tzinfo=timezone.utc)


def _utc_month_start(now: datetime | None = None) -> datetime:
    n = now or datetime.now(timezone.utc)
    return datetime(n.year, n.month, 1, tzinfo=timezone.utc)


_SPENDING_STATES = (
    JobState.Queued,
    JobState.Running,
    JobState.Completed,
    JobState.Failed,
)


async def recent_spend_for_user(
    session: AsyncSession,
    user_id: uuid.UUID,
    *,
    now: datetime | None = None,
) -> RecentSpend:
    n = now or datetime.now(timezone.utc)
    day0 = _utc_day_start(n)
    mon0 = _utc_month_start(n)
    daily = (
        await session.execute(
            select(func.coalesce(func.sum(Job.dollar_cost), 0))
            .where(Job.user_id == user_id)
            .where(Job.created_at >= day0)
            .where(Job.state.in_(_SPENDING_STATES))
        )
    ).scalar_one()
    monthly = (
        await session.execute(
            select(func.coalesce(func.sum(Job.dollar_cost), 0))
            .where(Job.user_id == user_id)
            .where(Job.created_at >= mon0)
            .where(Job.state.in_(_SPENDING_STATES))
        )
    ).scalar_one()
    return RecentSpend(
        daily=Decimal(str(daily or 0)),
        monthly=Decimal(str(monthly or 0)),
    )


# A user with no `Budget` row gets these defaults — generous enough for
# a local provider but tight enough to refuse a casual IBM run.
_FALLBACK_DAILY = Decimal("1.00")
_FALLBACK_MONTHLY = Decimal("10.00")
_FALLBACK_MAX_SHOTS = 10_000


def check_budget(
    *,
    budget: Budget | None,
    shots: int,
    chip: ChipInfo,
    recent_daily_spend: Decimal = Decimal("0"),
    recent_monthly_spend: Decimal = Decimal("0"),
) -> CostDecision:
    cost = dollar_cost(shots, chip)
    daily = budget.daily_usd if budget else _FALLBACK_DAILY
    monthly = budget.monthly_usd if budget else _FALLBACK_MONTHLY
    max_shots = (
        budget.max_shots_per_job if budget else _FALLBACK_MAX_SHOTS
    )

    if shots > max_shots:
        return CostDecision(
            allow=False,
            reason="shots_exceeds_max_shots_per_job",
            detail={
                "shots": shots,
                "max_shots_per_job": max_shots,
                "suggestion": (
                    f"reduce shots to <= {max_shots} or raise the "
                    "max_shots_per_job in your budget."
                ),
            },
        )

    daily_remaining = Decimal(daily) - Decimal(recent_daily_spend)
    monthly_remaining = Decimal(monthly) - Decimal(recent_monthly_spend)

    if cost > daily_remaining:
        return CostDecision(
            allow=False,
            reason="exceeds_daily_budget",
            detail={
                "dollar_cost": str(cost),
                "daily_usd": str(daily),
                "recent_daily_spend": str(recent_daily_spend),
                "daily_remaining": str(daily_remaining),
                "suggestion": (
                    "reduce shots, pick a cheaper target, or raise "
                    "the daily budget."
                ),
            },
        )
    if cost > monthly_remaining:
        return CostDecision(
            allow=False,
            reason="exceeds_monthly_budget",
            detail={
                "dollar_cost": str(cost),
                "monthly_usd": str(monthly),
                "recent_monthly_spend": str(recent_monthly_spend),
                "monthly_remaining": str(monthly_remaining),
            },
        )
    return CostDecision(
        allow=True,
        detail={
            "dollar_cost": str(cost),
            "daily_remaining": str(daily_remaining - cost),
            "monthly_remaining": str(monthly_remaining - cost),
        },
    )


__all__ = [
    "CostDecision",
    "RecentSpend",
    "check_budget",
    "dollar_cost",
    "recent_spend_for_user",
]
