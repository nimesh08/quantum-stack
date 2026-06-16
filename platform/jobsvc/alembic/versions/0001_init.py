"""init schema

Revision ID: 0001_init
Revises:
Create Date: 2026-06-16

"""
from __future__ import annotations

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op
from sqlalchemy.dialects.postgresql import JSONB, UUID as PGUUID


revision: str = "0001_init"
down_revision: Union[str, Sequence[str], None] = None
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


JOB_STATE = sa.Enum(
    "Submitted",
    "Queued",
    "Running",
    "Completed",
    "Rejected",
    "Failed",
    name="job_state",
)
SOURCE_KIND = sa.Enum("photon", "phonon", "spinor", name="source_kind")
USER_ROLE = sa.Enum("user", "admin", name="user_role")


def upgrade() -> None:
    op.create_table(
        "users",
        sa.Column("id", PGUUID(as_uuid=True), primary_key=True),
        sa.Column("email", sa.String(320), nullable=False, unique=True),
        sa.Column("password_hash", sa.String(255), nullable=False),
        sa.Column("role", USER_ROLE, nullable=False, server_default="user"),
        sa.Column(
            "created_at",
            sa.DateTime(timezone=True),
            nullable=False,
            server_default=sa.func.now(),
        ),
    )

    op.create_table(
        "api_keys",
        sa.Column("id", PGUUID(as_uuid=True), primary_key=True),
        sa.Column(
            "user_id",
            PGUUID(as_uuid=True),
            sa.ForeignKey("users.id", ondelete="CASCADE"),
            nullable=False,
        ),
        sa.Column("prefix", sa.String(8), nullable=False, index=True),
        sa.Column("hash", sa.String(255), nullable=False),
        sa.Column("label", sa.String(120), nullable=False, server_default=""),
        sa.Column("last_used_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("revoked_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column(
            "created_at",
            sa.DateTime(timezone=True),
            nullable=False,
            server_default=sa.func.now(),
        ),
    )
    op.create_index("ix_api_keys_prefix", "api_keys", ["prefix"])

    op.create_table(
        "budgets",
        sa.Column(
            "user_id",
            PGUUID(as_uuid=True),
            sa.ForeignKey("users.id", ondelete="CASCADE"),
            primary_key=True,
        ),
        sa.Column(
            "daily_usd",
            sa.Numeric(12, 4),
            nullable=False,
            server_default="1.00",
        ),
        sa.Column(
            "monthly_usd",
            sa.Numeric(12, 4),
            nullable=False,
            server_default="10.00",
        ),
        sa.Column(
            "max_shots_per_job",
            sa.Integer(),
            nullable=False,
            server_default="10000",
        ),
        sa.Column(
            "updated_at",
            sa.DateTime(timezone=True),
            nullable=False,
            server_default=sa.func.now(),
        ),
    )

    op.create_table(
        "jobs",
        sa.Column("id", PGUUID(as_uuid=True), primary_key=True),
        sa.Column(
            "user_id",
            PGUUID(as_uuid=True),
            sa.ForeignKey("users.id", ondelete="CASCADE"),
            nullable=False,
            index=True,
        ),
        sa.Column("name", sa.String(200), nullable=False, server_default=""),
        sa.Column("target", sa.String(64), nullable=False),
        sa.Column("shots", sa.Integer(), nullable=False),
        sa.Column("source", sa.Text(), nullable=False),
        sa.Column("source_kind", SOURCE_KIND, nullable=False),
        sa.Column("state", JOB_STATE, nullable=False, server_default="Submitted"),
        sa.Column("rejection_reason", sa.Text(), nullable=True),
        sa.Column("error_kind", sa.String(32), nullable=True),
        sa.Column("estimate", JSONB(), nullable=True),
        sa.Column("dollar_cost", sa.Numeric(12, 6), nullable=True),
        sa.Column("provider", sa.String(32), nullable=True),
        sa.Column(
            "provider_job_id",
            sa.String(128),
            nullable=True,
            unique=True,
        ),
        sa.Column("claimed_by", sa.String(64), nullable=True),
        sa.Column("claim_expires_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column(
            "created_at",
            sa.DateTime(timezone=True),
            nullable=False,
            server_default=sa.func.now(),
        ),
        sa.Column("queued_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("started_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("finished_at", sa.DateTime(timezone=True), nullable=True),
    )
    op.create_index("ix_jobs_user_id", "jobs", ["user_id"])
    op.create_index("ix_jobs_state", "jobs", ["state"])
    op.create_index(
        "ix_jobs_state_queued_at",
        "jobs",
        ["state", "queued_at"],
    )

    op.create_table(
        "results",
        sa.Column(
            "job_id",
            PGUUID(as_uuid=True),
            sa.ForeignKey("jobs.id", ondelete="CASCADE"),
            primary_key=True,
        ),
        sa.Column("counts", JSONB(), nullable=False),
        sa.Column("shots", sa.Integer(), nullable=False),
        sa.Column("raw_provider_payload", JSONB(), nullable=True),
        sa.Column(
            "created_at",
            sa.DateTime(timezone=True),
            nullable=False,
            server_default=sa.func.now(),
        ),
    )

    op.create_table(
        "calibration_snapshots",
        sa.Column("id", PGUUID(as_uuid=True), primary_key=True),
        sa.Column("chip_id", sa.String(64), nullable=False, index=True),
        sa.Column(
            "fetched_at",
            sa.DateTime(timezone=True),
            nullable=False,
            server_default=sa.func.now(),
        ),
        sa.Column("source_provider", sa.String(32), nullable=False),
        sa.Column("body", JSONB(), nullable=True),
        sa.Column("sha256", sa.String(64), nullable=True),
        sa.Column("written_path", sa.String(512), nullable=True),
        sa.Column(
            "ok", sa.Boolean(), nullable=False, server_default=sa.true()
        ),
        sa.Column("error", sa.Text(), nullable=True),
    )
    op.create_index(
        "ix_calibration_chip_fetched",
        "calibration_snapshots",
        ["chip_id", "fetched_at"],
    )

    op.create_table(
        "audit_log",
        sa.Column("id", PGUUID(as_uuid=True), primary_key=True),
        sa.Column(
            "user_id",
            PGUUID(as_uuid=True),
            sa.ForeignKey("users.id", ondelete="SET NULL"),
            nullable=True,
            index=True,
        ),
        sa.Column("action", sa.String(64), nullable=False, index=True),
        sa.Column("target_type", sa.String(32), nullable=True),
        sa.Column("target_id", sa.String(64), nullable=True),
        sa.Column("detail", JSONB(), nullable=True),
        sa.Column("ip", sa.String(64), nullable=True),
        sa.Column("ua", sa.String(255), nullable=True),
        sa.Column(
            "at",
            sa.DateTime(timezone=True),
            nullable=False,
            server_default=sa.func.now(),
        ),
    )
    op.create_index("ix_audit_log_action", "audit_log", ["action"])


def downgrade() -> None:
    op.drop_table("audit_log")
    op.drop_table("calibration_snapshots")
    op.drop_table("results")
    op.drop_table("jobs")
    op.drop_table("budgets")
    op.drop_table("api_keys")
    op.drop_table("users")
    bind = op.get_bind()
    JOB_STATE.drop(bind, checkfirst=True)
    SOURCE_KIND.drop(bind, checkfirst=True)
    USER_ROLE.drop(bind, checkfirst=True)
