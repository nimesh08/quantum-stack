"""M2 — GET /api/v1/targets reads the spinor registry."""

from __future__ import annotations

import pytest


@pytest.mark.asyncio
async def test_targets_returns_registry_chips(client_env) -> None:
    c = client_env["client"]
    r = await c.get("/api/v1/targets")
    assert r.status_code == 200
    items = r.json()
    ids = {it["id"] for it in items}
    # generic is synthetic; ibm_heron_r2 / ionq_forte / ionq_aria_proto /
    # quantinuum_helios are the four shipped chip YAMLs.
    assert "generic" in ids
    assert "ibm_heron_r2" in ids
    assert "ionq_forte" in ids
    assert "quantinuum_helios" in ids


@pytest.mark.asyncio
async def test_target_returns_pricing(client_env) -> None:
    c = client_env["client"]
    r = await c.get("/api/v1/targets/ibm_heron_r2")
    assert r.status_code == 200
    body = r.json()
    assert body["provider"] == "ibm"
    assert body["pricing_per_shot_usd"] > 0


@pytest.mark.asyncio
async def test_unknown_target_404(client_env) -> None:
    c = client_env["client"]
    r = await c.get("/api/v1/targets/nope")
    assert r.status_code == 404
