"""M4 — cassette-mode submission test.

Drives the worker through `spinor_submit` (already installed via
`pip install -e quantum-stack/spinor/submit/python`) using its
`SPINOR_SUBMIT_MODE=cassette` mode against a chip whose `provider`
matches the recorded cassette's path.

If `spinor_submit` is not importable on this host the test is skipped.
"""

from __future__ import annotations

import os
import uuid

import pytest
from sqlalchemy import select

pytest.importorskip("spinor_submit")

from jobsvc.audit import record  # noqa: E402
from jobsvc.auth.passwords import hash_password  # noqa: E402
from jobsvc.models import (  # noqa: E402
    Budget,
    Job,
    JobState,
    SourceKind,
    User,
    UserRole,
)
from jobsvc.worker import process_one  # noqa: E402


BELL = "target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n"


@pytest.mark.asyncio
async def test_cassette_mode_ibm_bell(client_env, monkeypatch) -> None:
    monkeypatch.setenv("SPINOR_SUBMIT_MODE", "cassette")

    sm = client_env["sm"]
    async with sm() as s:
        u = User(
            email=f"cas-{uuid.uuid4().hex[:8]}@x.com",
            password_hash=hash_password("password123"),
            role=UserRole.user,
        )
        s.add(u)
        await s.flush()
        s.add(Budget(user_id=u.id))
        # Note: ibm_heron_r2 has per_shot_usd=0.00033; default daily
        # budget is $1.00, so 100 shots ($0.033) fits comfortably.
        j = Job(
            user_id=u.id,
            target="ibm_heron_r2",
            shots=1000,
            source=BELL,
            source_kind=SourceKind.spinor,
            provider="ibm",
            name="bell",  # cassette name
        )
        j.transition(JobState.Queued)
        s.add(j)
        await s.commit()
        jid = j.id

    async with sm() as s:
        await process_one(s, worker_id="cas")

    async with sm() as s:
        loaded = (
            await s.execute(select(Job).where(Job.id == jid))
        ).scalar_one()
        assert loaded.state is JobState.Completed, loaded.rejection_reason
