"""M2 — POST /api/v1/jobs end-to-end (compile + queue or 402)."""

from __future__ import annotations

import pytest


BELL = """target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q
"""


@pytest.mark.asyncio
async def test_post_jobs_bell_succeeds(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)

    r = await c.post(
        "/api/v1/jobs",
        json={
            "source": BELL,
            "source_kind": "spinor",
            "target": "generic",
            "shots": 100,
        },
        headers=auth,
    )
    assert r.status_code == 201, r.text
    body = r.json()
    assert body["state"] == "Queued"
    assert body["estimate"]["num_qubits"] >= 2
    assert body["estimate"]["two_qubit_count"] >= 1
    assert body["target"] == "generic"
    assert body["shots"] == 100


@pytest.mark.asyncio
async def test_post_jobs_unknown_target(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)

    r = await c.post(
        "/api/v1/jobs",
        json={
            "source": BELL,
            "source_kind": "spinor",
            "target": "made_up_chip",
            "shots": 10,
        },
        headers=auth,
    )
    assert r.status_code == 400
    assert "unknown target" in r.text


@pytest.mark.asyncio
async def test_post_jobs_compile_error(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)

    r = await c.post(
        "/api/v1/jobs",
        json={
            "source": "",  # empty source -> stub returns ok=False
            "source_kind": "spinor",
            "target": "generic",
            "shots": 1,
        },
        headers=auth,
    )
    assert r.status_code == 422  # pydantic min_length=1


@pytest.mark.asyncio
async def test_post_jobs_requires_auth(client_env) -> None:
    c = client_env["client"]
    r = await c.post(
        "/api/v1/jobs",
        json={
            "source": BELL,
            "source_kind": "spinor",
            "target": "generic",
            "shots": 1,
        },
    )
    assert r.status_code == 401


@pytest.mark.asyncio
async def test_post_jobs_oversize_source_rejected(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    big = "x" * 200_001
    r = await c.post(
        "/api/v1/jobs",
        json={
            "source": big,
            "source_kind": "spinor",
            "target": "generic",
            "shots": 1,
        },
        headers=auth,
    )
    assert r.status_code == 422
