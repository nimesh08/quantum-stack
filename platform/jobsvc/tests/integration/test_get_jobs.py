"""M2 — GET /api/v1/jobs and /api/v1/jobs/{id} including pagination."""

from __future__ import annotations

import pytest


BELL = "target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n"


async def _submit(client, auth, **overrides):
    body = {
        "source": BELL,
        "source_kind": "spinor",
        "target": "generic",
        "shots": 100,
    }
    body.update(overrides)
    r = await client.post("/api/v1/jobs", json=body, headers=auth)
    assert r.status_code == 201, r.text
    return r.json()


@pytest.mark.asyncio
async def test_get_job_returns_full_detail(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    created = await _submit(c, auth)
    r = await c.get(f"/api/v1/jobs/{created['id']}", headers=auth)
    assert r.status_code == 200
    body = r.json()
    assert body["id"] == created["id"]
    assert body["source"] == BELL
    assert body["result"] is None  # no worker yet


@pytest.mark.asyncio
async def test_get_job_returns_404_for_other_user(client_env) -> None:
    c = client_env["client"]
    user_auth = client_env["auth"](client_env["user"].access_token)
    admin_auth = client_env["auth"](client_env["admin"].access_token)
    created = await _submit(c, user_auth)
    # Admin can see all (M7 contract): admin GET on user's job succeeds.
    r = await c.get(f"/api/v1/jobs/{created['id']}", headers=admin_auth)
    assert r.status_code == 200

    # But: a second user cannot. Make a third user via admin and try.
    r = await c.post(
        "/api/v1/admin/users",
        json={"email": "third@x.com", "password": "password123"},
        headers=admin_auth,
    )
    assert r.status_code == 201
    third = r.json()
    # Login as third
    r = await c.post(
        "/api/v1/login",
        json={"email": "third@x.com", "password": "password123"},
    )
    assert r.status_code == 200
    third_token = r.json()["access_token"]
    r = await c.get(
        f"/api/v1/jobs/{created['id']}",
        headers={"Authorization": f"Bearer {third_token}"},
    )
    assert r.status_code == 404


@pytest.mark.asyncio
async def test_jobs_pagination_returns_correct_pages(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    ids = [
        (await _submit(c, auth, name=f"j{i}"))["id"] for i in range(7)
    ]

    r = await c.get("/api/v1/jobs?limit=3", headers=auth)
    assert r.status_code == 200
    page1 = r.json()
    assert len(page1["items"]) == 3
    assert page1["next_cursor"]

    r = await c.get(
        f"/api/v1/jobs?limit=3&cursor={page1['next_cursor']}", headers=auth
    )
    page2 = r.json()
    assert len(page2["items"]) == 3
    assert page2["next_cursor"]

    r = await c.get(
        f"/api/v1/jobs?limit=3&cursor={page2['next_cursor']}", headers=auth
    )
    page3 = r.json()
    assert len(page3["items"]) == 1
    assert page3["next_cursor"] is None

    seen = [i["id"] for i in page1["items"] + page2["items"] + page3["items"]]
    assert sorted(seen) == sorted(ids)


@pytest.mark.asyncio
async def test_list_jobs_filters_by_state(client_env) -> None:
    c = client_env["client"]
    auth = client_env["auth"](client_env["user"].access_token)
    await _submit(c, auth)
    r = await c.get("/api/v1/jobs?state=Queued", headers=auth)
    assert r.status_code == 200
    items = r.json()["items"]
    assert all(i["state"] == "Queued" for i in items)
    assert len(items) == 1


@pytest.mark.asyncio
async def test_list_jobs_only_caller_visible(client_env) -> None:
    c = client_env["client"]
    user_auth = client_env["auth"](client_env["user"].access_token)
    admin_auth = client_env["auth"](client_env["admin"].access_token)
    await _submit(c, user_auth)

    # Without ?user_id= the admin sees every user's jobs.
    r = await c.get("/api/v1/jobs", headers=admin_auth)
    assert r.status_code == 200
    items = r.json()["items"]
    assert any(i["user_id"] == str(client_env["user"].id) for i in items)

    # The admin can scope to a specific user.
    r = await c.get(
        f"/api/v1/jobs?user_id={client_env['user'].id}", headers=admin_auth
    )
    items = r.json()["items"]
    assert all(i["user_id"] == str(client_env["user"].id) for i in items)
    assert len(items) == 1
