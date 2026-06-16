"""Smoke: every table the models declare gets created when
`Base.metadata.create_all` runs (which is what alembic 0001_init mirrors
in PG-specific syntax). This guards against a model added without a
matching migration."""

from __future__ import annotations

import pytest
from sqlalchemy import inspect
from sqlalchemy.ext.asyncio import AsyncEngine

from jobsvc.db import Base


@pytest.mark.asyncio
async def test_metadata_creates_all_expected_tables(
    sqlite_engine: AsyncEngine,
) -> None:
    expected = {
        "users",
        "api_keys",
        "budgets",
        "jobs",
        "results",
        "calibration_snapshots",
        "audit_log",
    }

    async with sqlite_engine.begin() as conn:
        names = await conn.run_sync(
            lambda sync_conn: set(inspect(sync_conn).get_table_names())
        )
    assert expected.issubset(names), names


def test_metadata_table_count_matches_model_count() -> None:
    # If you add a table here, also add it to the alembic migration.
    assert len(Base.metadata.tables) == 7
