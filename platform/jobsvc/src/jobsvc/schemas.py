"""Pydantic models — the wire format for the API."""

from __future__ import annotations

import uuid
from datetime import datetime
from decimal import Decimal
from typing import Any

from pydantic import BaseModel, ConfigDict, Field, field_validator

from .models import JobState, SourceKind, UserRole


# ---------- Auth ----------


class LoginRequest(BaseModel):
    email: str
    password: str


class TokenPair(BaseModel):
    access_token: str
    refresh_token: str
    token_type: str = "bearer"
    expires_in: int


class UserOut(BaseModel):
    id: uuid.UUID
    email: str
    role: UserRole
    created_at: datetime

    model_config = ConfigDict(from_attributes=True)


class CreateUser(BaseModel):
    email: str
    password: str = Field(min_length=8, max_length=200)
    role: UserRole = UserRole.user


class CreateApiKey(BaseModel):
    label: str = Field(default="", max_length=120)


class ApiKeyOut(BaseModel):
    id: uuid.UUID
    prefix: str
    label: str
    created_at: datetime
    revoked_at: datetime | None = None
    last_used_at: datetime | None = None

    model_config = ConfigDict(from_attributes=True)


class ApiKeyCreated(ApiKeyOut):
    plaintext: str  # returned ONCE


# ---------- Budget ----------


class BudgetOut(BaseModel):
    daily_usd: Decimal
    monthly_usd: Decimal
    max_shots_per_job: int

    model_config = ConfigDict(from_attributes=True)


class BudgetPatch(BaseModel):
    daily_usd: Decimal | None = None
    monthly_usd: Decimal | None = None
    max_shots_per_job: int | None = Field(default=None, ge=1, le=10_000_000)

    @field_validator("daily_usd", "monthly_usd")
    @classmethod
    def _non_negative(cls, v: Decimal | None) -> Decimal | None:
        if v is None:
            return v
        if v < 0:
            raise ValueError("must be non-negative")
        return v


# ---------- Targets ----------


class TargetOut(BaseModel):
    id: str
    provider: str
    qubits: int
    native_gates: list[str]
    coupling_topology: str
    supports: dict[str, Any]
    pricing_per_shot_usd: float


# ---------- Jobs ----------


class JobCreate(BaseModel):
    """Request body for ``POST /api/v1/jobs``.

    Attributes:
        source: Program text (1-200 000 chars). Format depends on
            ``source_kind``.
        source_kind: One of ``photon | phonon | spinor``.
        target: Chip id from
            [`GET /targets`][jobsvc.routers.targets]. Use
            ``"generic"`` for the portable target.
        shots: Number of shots to run (1..10 000 000).
        name: Optional human label; surfaces in
            [`Job.name`][jobsvc.models.Job] and is the cassette key
            for cassette-mode submissions.

    Example:
        ```json
        {
          "source": "target generic\\nqubit q[2]\\nh q[0]\\ncx q[0], q[1]\\n",
          "source_kind": "spinor",
          "target": "ibm_heron_r2",
          "shots": 1000,
          "name": "bell"
        }
        ```
    """

    source: str = Field(min_length=1, max_length=200_000)
    source_kind: SourceKind = SourceKind.spinor
    target: str = Field(min_length=1, max_length=64)
    shots: int = Field(default=1000, ge=1, le=10_000_000)
    name: str = Field(default="", max_length=200)


class EstimateOut(BaseModel):
    """Resource estimate as exposed in ``Job.estimate``."""

    num_qubits: int
    depth: int
    two_qubit_count: int
    t_count: int


class JobOut(BaseModel):
    """Compact view of a job (no source, no result).

    Used by ``GET /jobs`` (list) and as the response body of
    ``POST /jobs``. For the full view see
    [`JobDetail`][jobsvc.schemas.JobDetail].
    """

    id: uuid.UUID
    user_id: uuid.UUID
    name: str
    target: str
    shots: int
    source_kind: SourceKind
    state: JobState
    rejection_reason: str | None = None
    error_kind: str | None = None
    estimate: EstimateOut | None = None
    dollar_cost: Decimal | None = None
    provider: str | None = None
    created_at: datetime
    queued_at: datetime | None = None
    started_at: datetime | None = None
    finished_at: datetime | None = None

    model_config = ConfigDict(from_attributes=True)


class HistogramOut(BaseModel):
    """Measurement histogram returned with a Completed job."""

    counts: dict[str, int]
    shots: int


class JobDetail(JobOut):
    """Full view of a job: ``JobOut`` plus ``source`` and ``result``."""

    source: str
    result: HistogramOut | None = None


class JobList(BaseModel):
    """Paginated listing returned by ``GET /jobs``.

    Attributes:
        items: Page of jobs (most-recent-first).
        next_cursor: Opaque cursor to pass back as the ``cursor``
            query param for the next page. ``None`` when there are
            no more results.
    """

    items: list[JobOut]
    next_cursor: str | None = None


class HealthOut(BaseModel):
    status: str
    version: str
    engine_available: bool


__all__ = [
    "ApiKeyCreated",
    "ApiKeyOut",
    "BudgetOut",
    "BudgetPatch",
    "CreateApiKey",
    "CreateUser",
    "EstimateOut",
    "HealthOut",
    "HistogramOut",
    "JobCreate",
    "JobDetail",
    "JobList",
    "JobOut",
    "LoginRequest",
    "TargetOut",
    "TokenPair",
    "UserOut",
]
