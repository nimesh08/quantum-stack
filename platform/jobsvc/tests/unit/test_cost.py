"""M3 — unit tests for cost.dollar_cost and cost.check_budget."""

from __future__ import annotations

from decimal import Decimal

import pytest

from jobsvc.cost import CostDecision, check_budget, dollar_cost
from jobsvc.models import Budget
from jobsvc.registry import ChipInfo


def _chip(price: str = "0.001") -> ChipInfo:
    return ChipInfo(
        id="test_chip",
        provider="local",
        qubits=4,
        native_gates=("h", "cx"),
        coupling_topology="all_to_all",
        supports={},
        pricing_per_shot_usd=Decimal(price),
    )


def _budget(daily="1.00", monthly="10.00", maxshots=10_000) -> Budget:
    b = Budget(
        user_id="00000000-0000-0000-0000-000000000000",
        daily_usd=Decimal(daily),
        monthly_usd=Decimal(monthly),
        max_shots_per_job=maxshots,
    )
    return b


def test_dollar_cost_quantizes_to_microdollars() -> None:
    c = dollar_cost(1000, _chip("0.00033"))
    assert c == Decimal("0.330000")


def test_dollar_cost_zero_pricing_chip() -> None:
    c = dollar_cost(1_000_000, _chip("0.0"))
    assert c == Decimal("0E-6") or c == Decimal("0.000000")


def test_dollar_cost_negative_shots_raises() -> None:
    with pytest.raises(ValueError):
        dollar_cost(-1, _chip())


# ---------- check_budget ----------


def test_under_daily_budget_allows() -> None:
    d = check_budget(budget=_budget(), shots=100, chip=_chip("0.001"))
    assert d.allow is True
    assert d.detail["dollar_cost"] == "0.100000"


def test_over_daily_budget_rejects() -> None:
    d = check_budget(budget=_budget(daily="0.05"), shots=1000, chip=_chip("0.001"))
    assert d.allow is False
    assert d.reason == "exceeds_daily_budget"
    assert "suggestion" in d.detail


def test_over_monthly_budget_rejects() -> None:
    d = check_budget(
        budget=_budget(daily="100", monthly="0.5"),
        shots=1000,
        chip=_chip("0.001"),
    )
    assert d.allow is False
    assert d.reason == "exceeds_monthly_budget"


def test_recent_spend_consumes_remaining() -> None:
    d = check_budget(
        budget=_budget(daily="1.00"),
        shots=900,
        chip=_chip("0.001"),
        recent_daily_spend=Decimal("0.5"),
    )
    # 0.50 spent + 0.90 cost = 1.40 > 1.00
    assert d.allow is False


def test_max_shots_per_job_takes_precedence() -> None:
    d = check_budget(
        budget=_budget(maxshots=10),
        shots=11,
        chip=_chip("0.0"),
    )
    assert d.allow is False
    assert d.reason == "shots_exceeds_max_shots_per_job"


def test_zero_pricing_chip_always_allowed() -> None:
    d = check_budget(
        budget=_budget(daily="0.00", monthly="0.00", maxshots=1_000_000),
        shots=1_000_000,
        chip=_chip("0.0"),
    )
    assert d.allow is True


def test_no_budget_uses_fallback_defaults() -> None:
    d = check_budget(budget=None, shots=10, chip=_chip("0.001"))
    assert d.allow is True
    d = check_budget(budget=None, shots=2_000_000, chip=_chip("0.0"))
    assert d.allow is False
    assert d.reason == "shots_exceeds_max_shots_per_job"


def test_decimal_precision_microcents() -> None:
    d = check_budget(
        budget=_budget(daily="0.0001"),
        shots=1,
        chip=_chip("0.0001"),
    )
    assert d.allow is True


def test_negative_budget_rejects_everything_but_zero() -> None:
    """A negative budget is non-sense; check_budget treats it as zero."""
    d = check_budget(budget=_budget(daily="-5.00"), shots=1, chip=_chip("0.001"))
    assert d.allow is False
