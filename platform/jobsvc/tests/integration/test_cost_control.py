"""M3 — cost control end-to-end through POST /jobs."""

from __future__ import annotations

from decimal import Decimal

import pytest


BELL = "target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n"


@pytest.mark.asyncio
async def test_under_budget_proceeds(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    r = await c.post(
        "/api/v1/jobs",
        json={
            "source": BELL, "source_kind": "spinor",
            "target": "ibm_heron_r2", "shots": 100,
        },
        headers=auth,
    )
    assert r.status_code == 201, r.text
    assert r.json()["state"] == "Queued"


@pytest.mark.asyncio
async def test_over_budget_rejected_up_front(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)

    # Tighten daily to $0.10.
    await c.patch("/api/v1/me/budget", json={"daily_usd": "0.10"}, headers=auth)

    # Bell on IBM Heron r2 at 1000 shots costs $0.33.
    r = await c.post(
        "/api/v1/jobs",
        json={
            "source": BELL, "source_kind": "spinor",
            "target": "ibm_heron_r2", "shots": 1000,
        },
        headers=auth,
    )
    assert r.status_code == 402
    body = r.json()["detail"]
    assert body["reason"] == "exceeds_daily_budget"
    assert body["dollar_cost"] == "0.330000"

    # Job persisted as Rejected.
    r = await c.get("/api/v1/jobs?state=Rejected", headers=auth)
    items = r.json()["items"]
    assert len(items) == 1
    assert items[0]["state"] == "Rejected"
    assert items[0]["rejection_reason"] == "exceeds_daily_budget"


@pytest.mark.asyncio
async def test_local_target_zero_cost_always_proceeds(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    await c.patch("/api/v1/me/budget", json={"daily_usd": "0.00",
                                              "monthly_usd": "0.00"},
                  headers=auth)
    r = await c.post(
        "/api/v1/jobs",
        json={
            "source": BELL, "source_kind": "spinor",
            "target": "generic", "shots": 1000,
        },
        headers=auth,
    )
    assert r.status_code == 201


@pytest.mark.asyncio
async def test_max_shots_per_job_enforced(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    await c.patch(
        "/api/v1/me/budget", json={"max_shots_per_job": 50}, headers=auth
    )
    r = await c.post(
        "/api/v1/jobs",
        json={
            "source": BELL, "source_kind": "spinor",
            "target": "generic", "shots": 100,
        },
        headers=auth,
    )
    assert r.status_code == 402
    assert r.json()["detail"]["reason"] == "shots_exceeds_max_shots_per_job"


@pytest.mark.asyncio
async def test_recent_spend_blocks_subsequent_jobs(client_env) -> None:
    """First job spends most of the daily budget; second should reject."""
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    # $1.00 daily; ibm_heron_r2 @ 0.00033/shot. 2500 shots = $0.825.
    r = await c.post(
        "/api/v1/jobs",
        json={
            "source": BELL, "source_kind": "spinor",
            "target": "ibm_heron_r2", "shots": 2500,
        },
        headers=auth,
    )
    assert r.status_code == 201

    # Second 2500-shot job: 0.825 + 0.825 = 1.65 > 1.00.
    r = await c.post(
        "/api/v1/jobs",
        json={
            "source": BELL, "source_kind": "spinor",
            "target": "ibm_heron_r2", "shots": 2500,
        },
        headers=auth,
    )
    assert r.status_code == 402
    assert r.json()["detail"]["reason"] == "exceeds_daily_budget"


@pytest.mark.asyncio
async def test_budget_get_initialises_default(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    r = await c.get("/api/v1/me/budget", headers=auth)
    assert r.status_code == 200
    body = r.json()
    assert Decimal(body["daily_usd"]) == Decimal("1.00")
