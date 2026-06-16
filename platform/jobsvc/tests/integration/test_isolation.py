"""M7 — multi-tenancy isolation + observability + admin behaviours."""

from __future__ import annotations

import pytest


BELL = "target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n"


@pytest.mark.asyncio
async def test_two_users_isolated(client_env) -> None:
    """User A submits a job; user B cannot see it via GET or list."""
    c = client_env["client"]
    user_auth = client_env["auth"](client_env["user"].access_token)
    admin_auth = client_env["auth"](client_env["admin"].access_token)

    # Create a third user as admin, login.
    r = await c.post(
        "/api/v1/admin/users",
        json={"email": "isolated@x.com", "password": "password123"},
        headers=admin_auth,
    )
    assert r.status_code == 201

    r = await c.post(
        "/api/v1/login",
        json={"email": "isolated@x.com", "password": "password123"},
    )
    third = r.json()["access_token"]

    # User submits.
    r = await c.post(
        "/api/v1/jobs",
        json={"source": BELL, "source_kind": "spinor",
              "target": "generic", "shots": 10},
        headers=user_auth,
    )
    jid = r.json()["id"]

    # Third user cannot see it (404).
    r = await c.get(
        f"/api/v1/jobs/{jid}",
        headers={"Authorization": f"Bearer {third}"},
    )
    assert r.status_code == 404

    r = await c.get(
        "/api/v1/jobs",
        headers={"Authorization": f"Bearer {third}"},
    )
    assert r.json()["items"] == []


@pytest.mark.asyncio
async def test_third_user_cannot_cancel_others_job(client_env) -> None:
    c = client_env["client"]
    user_auth = client_env["auth"](client_env["user"].access_token)
    admin_auth = client_env["auth"](client_env["admin"].access_token)

    await c.post(
        "/api/v1/admin/users",
        json={"email": "cant-cancel@x.com", "password": "password123"},
        headers=admin_auth,
    )
    r = await c.post(
        "/api/v1/login",
        json={"email": "cant-cancel@x.com", "password": "password123"},
    )
    third = r.json()["access_token"]

    r = await c.post(
        "/api/v1/jobs",
        json={"source": BELL, "source_kind": "spinor",
              "target": "generic", "shots": 1},
        headers=user_auth,
    )
    jid = r.json()["id"]

    r = await c.delete(
        f"/api/v1/jobs/{jid}", headers={"Authorization": f"Bearer {third}"},
    )
    assert r.status_code == 404


@pytest.mark.asyncio
async def test_admin_can_cancel_anyones_job(client_env) -> None:
    c = client_env["client"]
    user_auth = client_env["auth"](client_env["user"].access_token)
    admin_auth = client_env["auth"](client_env["admin"].access_token)
    r = await c.post(
        "/api/v1/jobs",
        json={"source": BELL, "source_kind": "spinor",
              "target": "generic", "shots": 1},
        headers=user_auth,
    )
    jid = r.json()["id"]
    r = await c.delete(f"/api/v1/jobs/{jid}", headers=admin_auth)
    assert r.status_code == 204


@pytest.mark.asyncio
async def test_metrics_includes_known_counters(client_env) -> None:
    c = client_env["client"]
    r = await c.get("/metrics")
    text = r.text
    for name in (
        "jobs_total",
        "queue_depth",
        "worker_lease_expirations_total",
        "errors_total",
        "calibration_refresh_total",
        "provider_latency_seconds",
        "job_duration_seconds",
    ):
        assert name in text, f"metric {name!r} missing"


@pytest.mark.asyncio
async def test_request_id_round_trip(client_env) -> None:
    c = client_env["client"]
    r = await c.get(
        "/healthz", headers={"x-request-id": "fixed-rid-123"}
    )
    assert r.status_code == 200
    assert r.headers.get("x-request-id") == "fixed-rid-123"
