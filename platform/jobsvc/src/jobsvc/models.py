"""SQLAlchemy 2.0 ORM models for Phase D.

Single source of truth for the schema. Alembic migrations are
generated from these classes (see `alembic/env.py`).

Schema overview:

  users               accounts (email + bcrypt password).
  api_keys            programmatic-access tokens (bcrypt-hashed).
  budgets             per-user $/day, $/month, max_shots_per_job.
  jobs                lifecycle entity; one row per submission.
  results             histogram + raw provider payload.
  calibration_snapshots  versioned per-chip calibration fetches.
  audit_log           every state transition + auth event.

Every state transition on `jobs` writes an `audit_log` row in the same
transaction, so the journal is the journal of the system.
"""

from __future__ import annotations

import enum
import uuid
from datetime import datetime, timezone
from decimal import Decimal
from typing import Any

from sqlalchemy import (
    JSON,
    Boolean,
    DateTime,
    Enum as SAEnum,
    ForeignKey,
    Integer,
    LargeBinary,
    Numeric,
    String,
    Text,
    UniqueConstraint,
    func,
)
from sqlalchemy.dialects.postgresql import JSONB, UUID as PGUUID
from sqlalchemy.orm import Mapped, mapped_column, relationship
from sqlalchemy.types import TypeDecorator

from .db import Base


# ---------------------------------------------------------------------------
# Cross-dialect helpers — let the same models work against Postgres
# (production) and SQLite (some unit tests).
# ---------------------------------------------------------------------------


class GUID(TypeDecorator):
    """UUID portable across Postgres and SQLite."""

    impl = String(36)
    cache_ok = True

    def load_dialect_impl(self, dialect):  # type: ignore[override]
        if dialect.name == "postgresql":
            return dialect.type_descriptor(PGUUID(as_uuid=True))
        return dialect.type_descriptor(String(36))

    def process_bind_param(self, value, dialect):  # type: ignore[override]
        if value is None:
            return None
        if isinstance(value, uuid.UUID):
            return value if dialect.name == "postgresql" else str(value)
        return value if dialect.name == "postgresql" else str(uuid.UUID(str(value)))

    def process_result_value(self, value, dialect):  # type: ignore[override]
        if value is None:
            return None
        return value if isinstance(value, uuid.UUID) else uuid.UUID(str(value))


def _json_col() -> JSON:  # pragma: no cover (trivial)
    return JSONB().with_variant(JSON(), "sqlite")


def _utc_now() -> datetime:
    return datetime.now(timezone.utc)


# ---------------------------------------------------------------------------
# Enums
# ---------------------------------------------------------------------------


class JobState(str, enum.Enum):
    """States a [`Job`][jobsvc.models.Job] passes through.

    Diagram of legal transitions: see ``LEGAL_TRANSITIONS``.

    Members:
        Submitted: Initial state, set by the API on POST.
        Queued: Cost check passed; awaiting a worker claim.
        Running: A worker holds the job (``claim_expires_at`` set).
        Completed: Worker stored a histogram in
            [`Result`][jobsvc.models.Result].
        Rejected: Cost check failed up front, or user cancelled.
        Failed: Worker errored — see ``error_kind`` to disambiguate
            "our" vs "provider".
    """

    Submitted = "Submitted"
    Queued = "Queued"
    Running = "Running"
    Completed = "Completed"
    Rejected = "Rejected"
    Failed = "Failed"


class SourceKind(str, enum.Enum):
    """Which source language is being submitted.

    Members:
        photon: Object-oriented Photon language. Lowered to Phonon by
            the Phase C frontend.
        phonon: Phonon source — the engine's native input.
        spinor: Hand-written Spinor assembly. Wrapped as a trivial
            Phonon program before lowering.
    """

    photon = "photon"
    phonon = "phonon"
    spinor = "spinor"


class UserRole(str, enum.Enum):
    """Authorisation role on a [`User`][jobsvc.models.User].

    Members:
        user: Can manage own jobs / budget / API keys.
        admin: Can list and cancel any user's jobs; can create
            users via ``/api/v1/admin/users``.
    """

    user = "user"
    admin = "admin"


# Legal transitions of the job state machine. Anything not in this
# table is rejected by `Job.transition`.
LEGAL_TRANSITIONS: set[tuple[JobState, JobState]] = {
    (JobState.Submitted, JobState.Queued),
    (JobState.Submitted, JobState.Rejected),
    (JobState.Queued, JobState.Running),
    (JobState.Queued, JobState.Rejected),  # cancellation pre-claim
    (JobState.Running, JobState.Completed),
    (JobState.Running, JobState.Failed),
    (JobState.Running, JobState.Queued),   # worker crash + lease expiry
}


TERMINAL_STATES = {JobState.Completed, JobState.Rejected, JobState.Failed}


class IllegalTransitionError(ValueError):
    """Attempted a state transition not in `LEGAL_TRANSITIONS`."""


# ---------------------------------------------------------------------------
# Models
# ---------------------------------------------------------------------------


class User(Base):
    __tablename__ = "users"

    id: Mapped[uuid.UUID] = mapped_column(
        GUID(), primary_key=True, default=uuid.uuid4
    )
    email: Mapped[str] = mapped_column(String(320), unique=True, nullable=False)
    password_hash: Mapped[str] = mapped_column(String(255), nullable=False)
    role: Mapped[UserRole] = mapped_column(
        SAEnum(UserRole, name="user_role"),
        nullable=False,
        default=UserRole.user,
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True),
        nullable=False,
        default=_utc_now,
        server_default=func.now(),
    )

    api_keys: Mapped[list["ApiKey"]] = relationship(
        back_populates="user", cascade="all, delete-orphan"
    )
    budget: Mapped["Budget | None"] = relationship(
        back_populates="user", uselist=False, cascade="all, delete-orphan"
    )
    jobs: Mapped[list["Job"]] = relationship(
        back_populates="user", cascade="all, delete-orphan"
    )


class ApiKey(Base):
    __tablename__ = "api_keys"

    id: Mapped[uuid.UUID] = mapped_column(
        GUID(), primary_key=True, default=uuid.uuid4
    )
    user_id: Mapped[uuid.UUID] = mapped_column(
        GUID(), ForeignKey("users.id", ondelete="CASCADE"), nullable=False
    )
    prefix: Mapped[str] = mapped_column(String(8), nullable=False, index=True)
    hash: Mapped[str] = mapped_column(String(255), nullable=False)
    label: Mapped[str] = mapped_column(String(120), nullable=False, default="")
    last_used_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    revoked_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True),
        nullable=False,
        default=_utc_now,
        server_default=func.now(),
    )

    user: Mapped[User] = relationship(back_populates="api_keys")


class Budget(Base):
    __tablename__ = "budgets"

    user_id: Mapped[uuid.UUID] = mapped_column(
        GUID(), ForeignKey("users.id", ondelete="CASCADE"), primary_key=True
    )
    daily_usd: Mapped[Decimal] = mapped_column(
        Numeric(12, 4), nullable=False, default=Decimal("1.00")
    )
    monthly_usd: Mapped[Decimal] = mapped_column(
        Numeric(12, 4), nullable=False, default=Decimal("10.00")
    )
    max_shots_per_job: Mapped[int] = mapped_column(
        Integer, nullable=False, default=10_000
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True),
        nullable=False,
        default=_utc_now,
        onupdate=_utc_now,
    )

    user: Mapped[User] = relationship(back_populates="budget")


class Job(Base):
    __tablename__ = "jobs"
    __table_args__ = (
        UniqueConstraint("provider_job_id", name="uq_jobs_provider_job_id"),
    )

    id: Mapped[uuid.UUID] = mapped_column(
        GUID(), primary_key=True, default=uuid.uuid4
    )
    user_id: Mapped[uuid.UUID] = mapped_column(
        GUID(),
        ForeignKey("users.id", ondelete="CASCADE"),
        nullable=False,
        index=True,
    )

    name: Mapped[str] = mapped_column(String(200), nullable=False, default="")
    target: Mapped[str] = mapped_column(String(64), nullable=False)
    shots: Mapped[int] = mapped_column(Integer, nullable=False)
    source: Mapped[str] = mapped_column(Text, nullable=False)
    source_kind: Mapped[SourceKind] = mapped_column(
        SAEnum(SourceKind, name="source_kind"),
        nullable=False,
    )

    state: Mapped[JobState] = mapped_column(
        SAEnum(JobState, name="job_state"),
        nullable=False,
        default=JobState.Submitted,
        server_default=JobState.Submitted.value,
        index=True,
    )
    rejection_reason: Mapped[str | None] = mapped_column(Text, nullable=True)
    error_kind: Mapped[str | None] = mapped_column(String(32), nullable=True)

    estimate: Mapped[dict[str, Any] | None] = mapped_column(_json_col(), nullable=True)
    dollar_cost: Mapped[Decimal | None] = mapped_column(
        Numeric(12, 6), nullable=True
    )

    provider: Mapped[str | None] = mapped_column(String(32), nullable=True)
    provider_job_id: Mapped[str | None] = mapped_column(String(128), nullable=True)

    claimed_by: Mapped[str | None] = mapped_column(String(64), nullable=True)
    claim_expires_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )

    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True),
        nullable=False,
        default=_utc_now,
        server_default=func.now(),
    )
    queued_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    started_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    finished_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )

    user: Mapped[User] = relationship(back_populates="jobs")
    result: Mapped["Result | None"] = relationship(
        back_populates="job", uselist=False, cascade="all, delete-orphan"
    )

    def __init__(self, **kw: Any) -> None:
        kw.setdefault("state", JobState.Submitted)
        super().__init__(**kw)

    # ----- state machine ------------------------------------------------

    def transition(
        self,
        new_state: JobState,
        *,
        reason: str | None = None,
        error_kind: str | None = None,
    ) -> None:
        """Apply a state-machine transition with timestamp stamping.

        Mutates the in-memory object — does **not** flush. The caller
        is expected to insert a corresponding
        [`AuditLog`][jobsvc.models.AuditLog] entry inside the same
        SQLAlchemy session/transaction (see
        [`jobsvc.audit.record`][jobsvc.audit.record]).

        Stamps ``queued_at``, ``started_at``, or ``finished_at`` on
        the corresponding state changes. Records ``rejection_reason``
        on ``Rejected`` and on ``Failed`` (along with
        ``error_kind`` when classifying our-vs-provider errors).

        Args:
            new_state: Target state. Must be a legal transition from
                the current state per
                [`LEGAL_TRANSITIONS`][jobsvc.models.LEGAL_TRANSITIONS].
            reason: Free-text explanation; persisted on
                ``rejection_reason``.
            error_kind: Either ``"our"`` (compile error, unknown
                chip, etc.) or ``"provider"`` (provider adapter
                raised). Only honoured on transitions to ``Failed``.

        Raises:
            IllegalTransitionError: ``(self.state, new_state)`` is
                not in
                [`LEGAL_TRANSITIONS`][jobsvc.models.LEGAL_TRANSITIONS].

        Example:
            >>> j = Job(user_id=None, target="generic", shots=1,
            ...         source="x", source_kind=SourceKind.spinor)
            >>> j.transition(JobState.Queued)
            >>> j.state is JobState.Queued
            True
        """
        cur = self.state
        if (cur, new_state) not in LEGAL_TRANSITIONS:
            raise IllegalTransitionError(
                f"illegal transition {cur.value} -> {new_state.value}"
            )
        now = _utc_now()
        self.state = new_state
        if new_state is JobState.Queued and self.queued_at is None:
            self.queued_at = now
        if new_state is JobState.Running and self.started_at is None:
            self.started_at = now
        if new_state in TERMINAL_STATES:
            self.finished_at = now
        if new_state is JobState.Rejected and reason:
            self.rejection_reason = reason
        if new_state is JobState.Failed:
            if reason:
                self.rejection_reason = reason
            if error_kind:
                self.error_kind = error_kind

    @property
    def is_terminal(self) -> bool:
        return self.state in TERMINAL_STATES


class Result(Base):
    __tablename__ = "results"

    job_id: Mapped[uuid.UUID] = mapped_column(
        GUID(),
        ForeignKey("jobs.id", ondelete="CASCADE"),
        primary_key=True,
    )
    counts: Mapped[dict[str, int]] = mapped_column(_json_col(), nullable=False)
    shots: Mapped[int] = mapped_column(Integer, nullable=False)
    raw_provider_payload: Mapped[dict[str, Any] | None] = mapped_column(
        _json_col(), nullable=True
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True),
        nullable=False,
        default=_utc_now,
        server_default=func.now(),
    )

    job: Mapped[Job] = relationship(back_populates="result")


class CalibrationSnapshot(Base):
    __tablename__ = "calibration_snapshots"

    id: Mapped[uuid.UUID] = mapped_column(
        GUID(), primary_key=True, default=uuid.uuid4
    )
    chip_id: Mapped[str] = mapped_column(String(64), index=True, nullable=False)
    fetched_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=_utc_now
    )
    source_provider: Mapped[str] = mapped_column(String(32), nullable=False)
    body: Mapped[dict[str, Any] | None] = mapped_column(_json_col(), nullable=True)
    sha256: Mapped[str | None] = mapped_column(String(64), nullable=True)
    written_path: Mapped[str | None] = mapped_column(String(512), nullable=True)
    ok: Mapped[bool] = mapped_column(Boolean, nullable=False, default=True)
    error: Mapped[str | None] = mapped_column(Text, nullable=True)


class AuditLog(Base):
    __tablename__ = "audit_log"

    id: Mapped[uuid.UUID] = mapped_column(
        GUID(), primary_key=True, default=uuid.uuid4
    )
    user_id: Mapped[uuid.UUID | None] = mapped_column(
        GUID(),
        ForeignKey("users.id", ondelete="SET NULL"),
        nullable=True,
        index=True,
    )
    action: Mapped[str] = mapped_column(String(64), nullable=False, index=True)
    target_type: Mapped[str | None] = mapped_column(String(32), nullable=True)
    target_id: Mapped[str | None] = mapped_column(String(64), nullable=True)
    detail: Mapped[dict[str, Any] | None] = mapped_column(_json_col(), nullable=True)
    ip: Mapped[str | None] = mapped_column(String(64), nullable=True)
    ua: Mapped[str | None] = mapped_column(String(255), nullable=True)
    at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=_utc_now
    )


__all__ = [
    "ApiKey",
    "AuditLog",
    "Budget",
    "CalibrationSnapshot",
    "IllegalTransitionError",
    "Job",
    "JobState",
    "LEGAL_TRANSITIONS",
    "Result",
    "SourceKind",
    "TERMINAL_STATES",
    "User",
    "UserRole",
]
