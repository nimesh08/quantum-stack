"""Cost control — M3, the first quantum-specific seam.

Compute the dollar cost of a job from its resource estimate and the
chip's `pricing.per_shot_usd`, then check it against the user's
[`Budget`][jobsvc.models.Budget] plus their recent spend window.

The seam is exercised from
[`jobsvc.routers.jobs.create_job`][] between "compile succeeded" and
"queue the job". The contract:

```python
decision = check_budget(budget=..., shots=..., chip=...,
                        recent_daily_spend=..., recent_monthly_spend=...)
if decision.allow:
    ...transition to Queued...
else:
    ...transition to Rejected, return 402 with decision.detail...
```

The recent-spend numbers come from
[`recent_spend_for_user`][jobsvc.cost.recent_spend_for_user], which sums
``dollar_cost`` over jobs in non-rejected states inside today's UTC day
and month respectively.
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
    """Outcome of [`check_budget`][jobsvc.cost.check_budget].

    Attributes:
        allow: True if the job may proceed to ``Queued``.
        reason: Stable, machine-readable rejection code when
            ``allow`` is False (one of ``exceeds_daily_budget``,
            ``exceeds_monthly_budget``,
            ``shots_exceeds_max_shots_per_job``). Empty when
            ``allow`` is True.
        detail: Structured payload returned to the API caller as the
            ``detail`` of an HTTP 402 response (or attached to the
            allow-side log line). Always includes ``dollar_cost``
            and the relevant remaining-budget figures.
    """

    allow: bool
    reason: str = ""
    detail: dict[str, Any] = field(default_factory=dict)


@dataclass(slots=True)
class RecentSpend:
    """Per-user spend rolled up over the daily and monthly windows.

    Both fields are dollar amounts as ``Decimal`` (microdollar
    precision). Returned by
    [`recent_spend_for_user`][jobsvc.cost.recent_spend_for_user].

    Attributes:
        daily: Sum of ``Job.dollar_cost`` over jobs created today
            (UTC) in non-rejected states.
        monthly: Same, over the current calendar month UTC.
    """

    daily: Decimal
    monthly: Decimal


def dollar_cost(shots: int, chip: ChipInfo) -> Decimal:
    """Compute the estimated dollar cost of a submission.

    Multiplies ``shots`` by ``chip.pricing_per_shot_usd`` and quantises
    the result to **microdollar** precision (six decimal places).

    Args:
        shots: Number of shots requested. Must be ``>= 0``.
        chip: Target chip info from
            [`jobsvc.registry`][jobsvc.registry]. Its
            ``pricing_per_shot_usd`` field is the multiplicand.

    Returns:
        Dollar cost as ``Decimal`` (e.g. ``Decimal('0.330000')``
        for 1000 shots at 0.00033 USD/shot).

    Raises:
        ValueError: ``shots`` is negative.

    Example:
        >>> from decimal import Decimal
        >>> from jobsvc.registry import ChipInfo
        >>> chip = ChipInfo(id="x", provider="local", qubits=1,
        ...                 native_gates=(), coupling_topology="all_to_all",
        ...                 supports={},
        ...                 pricing_per_shot_usd=Decimal("0.00033"))
        >>> dollar_cost(1000, chip)
        Decimal('0.330000')
    """
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
    """Sum the user's dollar spend over the daily and monthly windows.

    Counts jobs in **spending states** —
    ``Queued | Running | Completed | Failed``.
    ``Submitted`` jobs are still being decided (their cost is being
    checked right now). ``Rejected`` jobs never spent anything.

    Args:
        session: Async SQLAlchemy session bound to the request's
            DB connection.
        user_id: The user whose jobs we sum.
        now: Reference instant for the windows. Defaults to
            ``datetime.now(timezone.utc)``. Tests override it via
            ``freezegun``.

    Returns:
        [`RecentSpend`][jobsvc.cost.RecentSpend] with ``daily`` and
        ``monthly`` summed cost.

    Example:
        >>> # async, in a test:
        >>> # spend = await recent_spend_for_user(session, user.id)
        >>> # spend.daily, spend.monthly
        >>> True
        True
    """
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
    """Decide whether a job may proceed to ``Queued``.

    Three checks, evaluated in order:

    1.  ``shots > budget.max_shots_per_job`` →
        ``shots_exceeds_max_shots_per_job``.
    2.  ``cost > daily_remaining`` → ``exceeds_daily_budget``.
    3.  ``cost > monthly_remaining`` → ``exceeds_monthly_budget``.

    Where ``cost = dollar_cost(shots, chip)`` and the remaining budget
    is ``budget - recent_spend``. If ``budget`` is ``None`` (no
    ``Budget`` row yet) we fall back to **$1/day, $10/month, 10 000
    shots/job**.

    Args:
        budget: The user's [`Budget`][jobsvc.models.Budget] row, or
            None if not yet created (defaults applied).
        shots: Shots the user wants to run.
        chip: Target chip info (its pricing field is consulted).
        recent_daily_spend: Sum of ``Job.dollar_cost`` over today's
            spending-state jobs. Pass from
            [`recent_spend_for_user`][jobsvc.cost.recent_spend_for_user].
        recent_monthly_spend: Same, over the calendar month.

    Returns:
        [`CostDecision`][jobsvc.cost.CostDecision]. On allow, ``detail``
        carries the new remaining-budget figures so the API caller can
        surface them. On reject, ``detail`` carries enough to write a
        helpful 402 banner.

    Example:
        >>> from decimal import Decimal
        >>> from jobsvc.models import Budget
        >>> from jobsvc.registry import ChipInfo
        >>> b = Budget(user_id=None, daily_usd=Decimal("0.10"),
        ...            monthly_usd=Decimal("10.00"),
        ...            max_shots_per_job=10000)
        >>> chip = ChipInfo(id="x", provider="ibm", qubits=156,
        ...                 native_gates=(), coupling_topology="heavy_hex_156",
        ...                 supports={},
        ...                 pricing_per_shot_usd=Decimal("0.00033"))
        >>> d = check_budget(budget=b, shots=1000, chip=chip)
        >>> d.allow, d.reason
        (False, 'exceeds_daily_budget')
    """
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
