"""M7 prep — auth flows that M2 already exercises (login, /me, api keys,
admin-only)."""

from __future__ import annotations

import pytest


@pytest.mark.asyncio
async def test_login_returns_token_pair(client_env) -> None:
    c = client_env["client"]
    r = await c.post(
        "/api/v1/login",
        json={"email": "user@x.com", "password": "user-password"},
    )
    assert r.status_code == 200
    body = r.json()
    assert body["access_token"]
    assert body["refresh_token"]
    assert body["token_type"] == "bearer"


@pytest.mark.asyncio
async def test_login_rejects_bad_password(client_env) -> None:
    c = client_env["client"]
    r = await c.post(
        "/api/v1/login",
        json={"email": "user@x.com", "password": "wrong"},
    )
    assert r.status_code == 401


@pytest.mark.asyncio
async def test_me_returns_current_user(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    r = await c.get("/api/v1/me", headers=auth)
    assert r.status_code == 200
    assert r.json()["email"] == "user@x.com"


@pytest.mark.asyncio
async def test_admin_endpoint_blocked_for_user(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    r = await c.get("/api/v1/admin/users", headers=auth)
    assert r.status_code == 403


@pytest.mark.asyncio
async def test_admin_can_create_user(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["admin"].access_token)
    r = await c.post(
        "/api/v1/admin/users",
        json={"email": "newcomer@x.com", "password": "abcd1234"},
        headers=auth,
    )
    assert r.status_code == 201
    assert r.json()["email"] == "newcomer@x.com"


@pytest.mark.asyncio
async def test_api_key_round_trip(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    r = await c.post(
        "/api/v1/me/api-keys", json={"label": "test"}, headers=auth
    )
    assert r.status_code == 201
    body = r.json()
    plaintext = body["plaintext"]
    assert "." in plaintext
    # Use the key to call /me with X-API-Key only
    r = await c.get("/api/v1/me", headers={"X-API-Key": plaintext})
    assert r.status_code == 200

    # Revoke
    r = await c.delete(
        f"/api/v1/me/api-keys/{body['id']}", headers=auth
    )
    assert r.status_code == 204

    # The revoked key no longer authenticates
    r = await c.get("/api/v1/me", headers={"X-API-Key": plaintext})
    assert r.status_code == 401


@pytest.mark.asyncio
async def test_health_endpoints(client_env) -> None:
    c = client_env["client"]
    r = await c.get("/healthz")
    assert r.status_code == 200
    assert r.json()["status"] == "ok"

    r = await c.get("/readyz")
    assert r.status_code == 200

    r = await c.get("/metrics")
    assert r.status_code == 200
    assert b"jobs_total" in r.content


@pytest.mark.asyncio
async def test_openapi_paths_present(client_env) -> None:
    c = client_env["client"]
    r = await c.get("/api/openapi.json")
    assert r.status_code == 200
    paths = r.json()["paths"]
    expected = [
        "/api/v1/login",
        "/api/v1/me",
        "/api/v1/me/budget",
        "/api/v1/me/api-keys",
        "/api/v1/jobs",
        "/api/v1/jobs/{job_id}",
        "/api/v1/targets",
        "/api/v1/targets/{chip_id}",
        "/api/v1/admin/users",
        "/healthz",
        "/readyz",
        "/metrics",
    ]
    for p in expected:
        assert p in paths, f"missing {p}"
