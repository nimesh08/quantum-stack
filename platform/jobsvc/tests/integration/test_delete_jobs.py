"""M2 — DELETE /api/v1/jobs/{id} cancellation behaviour."""

from __future__ import annotations

import pytest


BELL = "target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n"


@pytest.mark.asyncio
async def test_delete_queued_job_cancels(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    r = await c.post(
        "/api/v1/jobs",
        json={"source": BELL, "source_kind": "spinor",
              "target": "generic", "shots": 1},
        headers=auth,
    )
    assert r.status_code == 201
    jid = r.json()["id"]

    r = await c.delete(f"/api/v1/jobs/{jid}", headers=auth)
    assert r.status_code == 204

    r = await c.get(f"/api/v1/jobs/{jid}", headers=auth)
    body = r.json()
    assert body["state"] == "Rejected"
    assert "cancelled" in (body.get("rejection_reason") or "")


@pytest.mark.asyncio
async def test_delete_unknown_job_404(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    import uuid
    r = await c.delete(f"/api/v1/jobs/{uuid.uuid4()}", headers=auth)
    assert r.status_code == 404
