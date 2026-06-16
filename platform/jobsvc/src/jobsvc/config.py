"""Settings — pydantic-settings, env-driven.

All env vars are prefixed `JOBSVC_`.
"""

from __future__ import annotations

import os
from functools import lru_cache
from pathlib import Path

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict


_DEFAULT_REGISTRY_ROOT = (
    Path(__file__).resolve().parents[4] / "spinor" / "registry"
)


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_prefix="JOBSVC_",
        env_file=os.environ.get("JOBSVC_ENV_FILE", ".env"),
        env_file_encoding="utf-8",
        extra="ignore",
    )

    # Database. asyncpg URL.
    database_url: str = Field(
        default="postgresql+asyncpg://jobsvc:jobsvc@localhost:5432/jobsvc",
        description="Async SQLAlchemy URL for Postgres.",
    )

    # SQL echo (debug only).
    sql_echo: bool = False

    # The registry root the spinor compiler reads chip YAMLs from.
    spinor_registry_root: Path = Field(
        default=_DEFAULT_REGISTRY_ROOT,
        description="Path to spinor/registry (chip YAMLs + topologies).",
    )

    # Submission mode for the Phase A adapters: cassette | live | local.
    spinor_submit_mode: str = "cassette"

    # JWT.
    jwt_secret: str = Field(
        default="dev-secret-change-me",
        description="HS256 signing key. MUST be changed in production.",
    )
    jwt_algorithm: str = "HS256"
    jwt_access_minutes: int = 60
    jwt_refresh_days: int = 14

    # Worker.
    worker_lease_seconds: int = 300
    worker_poll_interval_seconds: float = 1.0

    # Scheduler / calibration.
    calibration_cache_dir: Path = Field(
        default_factory=lambda: Path.home() / ".cache" / "spinor" / "calibration",
        description="Where calibration JSON is materialised for the compiler.",
    )

    # Observability.
    log_json: bool = True
    log_level: str = "INFO"

    # CORS — list of allowed origins. Empty = same-origin only.
    cors_origins: list[str] = Field(default_factory=list)


@lru_cache(maxsize=1)
def get_settings() -> Settings:
    return Settings()


__all__ = ["Settings", "get_settings"]
