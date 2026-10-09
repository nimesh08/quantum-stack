"""Versioned compiler/transport contracts. No credential values belong here."""
from __future__ import annotations

from dataclasses import asdict, dataclass, field
from math import isfinite
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 1


class QStackError(RuntimeError):
    def __init__(self, message: str, code: str = "INVALID_REQUEST", details: dict | None = None):
        super().__init__(message)
        self.code = code
        self.details = details


@dataclass
class CompiledArtifact:
    route: str
    target: str
    format: str
    payload: str | bytes
    physical_ir: dict[str, Any]
    target_snapshot: dict[str, Any] = field(default_factory=dict)
    manifest: dict[str, Any] = field(default_factory=dict)
    path: str | None = None
    schema_version: int = SCHEMA_VERSION

    def __post_init__(self):
        if self.schema_version != SCHEMA_VERSION:
            raise QStackError("Unsupported compiled artifact version", "ARTIFACT_INVALID")

    def program_text(self) -> str:
        return self.payload.decode("utf-8") if isinstance(self.payload, bytes) else self.payload

    def program_bytes(self) -> bytes:
        return self.payload if isinstance(self.payload, bytes) else self.payload.encode("utf-8")


@dataclass
class SubmissionOptions:
    shots: int = 1024
    name: str = "qstack"
    mode: str | None = None
    cost_cap_usd: float | None = None
    extra: dict[str, Any] = field(default_factory=dict)
    schema_version: int = SCHEMA_VERSION

    def __post_init__(self):
        if self.schema_version != SCHEMA_VERSION:
            raise QStackError("Unsupported submission options version")
        if type(self.shots) is not int or self.shots <= 0:
            raise QStackError("shots must be a positive integer")
        if self.mode not in {"live", "local", "cassette"}:
            raise QStackError("Select an execution mode: live, local or cassette", "MODE_REQUIRED")
        if self.cost_cap_usd is not None and (isinstance(self.cost_cap_usd, bool) or
                not isinstance(self.cost_cap_usd, (int, float)) or not isfinite(self.cost_cap_usd) or self.cost_cap_usd < 0):
            raise QStackError("cost cap must be nonnegative")


@dataclass
class JobReceipt:
    route: str
    target: str
    job_id: str
    metadata: dict[str, Any] = field(default_factory=dict)
    artifact_hash: str = ""
    mode: str = "live"
    schema_version: int = SCHEMA_VERSION

    def __post_init__(self):
        if self.schema_version != SCHEMA_VERSION:
            raise QStackError("Unsupported job receipt version")
        if self.mode not in {"live", "local", "cassette"}:
            raise QStackError("Invalid job receipt execution mode", "ARTIFACT_INVALID")
        if not isinstance(self.job_id, str) or not self.job_id:
            raise QStackError("Job receipt requires a provider job identifier", "ARTIFACT_INVALID")

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "JobReceipt":
        if data.get("schema_version", 1) != SCHEMA_VERSION:
            raise QStackError("Unsupported job receipt version")
        return cls(**data)


@dataclass
class ExecutionResult:
    route: str
    target: str
    job_id: str
    counts: dict[str, int] | None = None
    raw: Any = None
    metadata: dict[str, Any] = field(default_factory=dict)
    schema_version: int = SCHEMA_VERSION

    def __post_init__(self):
        if self.schema_version != SCHEMA_VERSION:
            raise QStackError("Unsupported result version")
        if self.counts is not None and (not isinstance(self.counts, dict) or any(
                not isinstance(key, str) or type(value) is not int or value < 0 for key, value in self.counts.items())):
            raise QStackError("Result counts must be actual nonnegative integer histograms; retain probabilities in raw", "INVALID_RESPONSE")

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "ExecutionResult":
        if data.get("schema_version", 1) != SCHEMA_VERSION:
            raise QStackError("Unsupported result version")
        return cls(**data)
