"""Configure jobsvc + spinor_submit + calibration via env vars.

The launcher's job is to set the right environment variables before
importing jobsvc, so jobsvc's pydantic-settings picks up our defaults
(SQLite, the bundled spinor registry, the cache dir under
``~/.local/share/heisenberg/``).

Author: Nimesh Cheedella <chnimesh0808@gmail.com>
"""

from __future__ import annotations

import os
import secrets
from pathlib import Path

from . import paths


def find_spinor_registry() -> Path | None:
    """Locate the bundled spinor registry.

    Search order:

    1. ``$SPINOR_REGISTRY_ROOT`` if set.
    2. ``<repo>/spinor/registry`` walking up from this file.
    3. ``$VIRTUAL_ENV/share/heisenberg/registry`` (post-install layout).
    4. ``/usr/share/heisenberg/registry`` (system install).
    """
    if env := os.environ.get("SPINOR_REGISTRY_ROOT"):
        p = Path(env).expanduser()
        if p.exists():
            return p
    here = Path(__file__).resolve()
    for parent in [here, *here.parents]:
        cand = parent / "spinor" / "registry"
        if cand.is_dir() and (cand / "chips").is_dir():
            return cand
    if venv := os.environ.get("VIRTUAL_ENV"):
        cand = Path(venv) / "share" / "heisenberg" / "registry"
        if cand.exists():
            return cand
    cand = Path("/usr/share/heisenberg/registry")
    if cand.exists():
        return cand
    return None


def _ensure_jwt_secret() -> str:
    secret_file = paths.data_dir() / "jwt.secret"
    if secret_file.exists():
        return secret_file.read_text(encoding="utf-8").strip()
    secret = secrets.token_urlsafe(48)
    secret_file.write_text(secret, encoding="utf-8")
    secret_file.chmod(0o600)
    return secret


def apply_environment(
    *,
    database_url: str | None = None,
    submit_mode: str = "cassette",
    log_json: bool = False,
) -> None:
    """Set environment variables that jobsvc + spinor_submit read.

    Idempotent. Caller can re-invoke with overrides.
    """
    os.environ.setdefault(
        "JOBSVC_DATABASE_URL",
        database_url or paths.default_sqlite_url(),
    )
    if database_url:
        os.environ["JOBSVC_DATABASE_URL"] = database_url

    os.environ.setdefault(
        "JOBSVC_CALIBRATION_CACHE_DIR",
        str(paths.calibration_cache_dir()),
    )
    os.environ.setdefault(
        "JOBSVC_LOG_JSON",
        "true" if log_json else "false",
    )
    os.environ.setdefault("JOBSVC_LOG_LEVEL", "INFO")
    os.environ.setdefault("JOBSVC_JWT_SECRET", _ensure_jwt_secret())

    if registry := find_spinor_registry():
        os.environ.setdefault("SPINOR_REGISTRY_ROOT", str(registry))
        os.environ.setdefault("JOBSVC_SPINOR_REGISTRY_ROOT", str(registry))

    os.environ.setdefault("SPINOR_SUBMIT_MODE", submit_mode)


__all__ = ["apply_environment", "find_spinor_registry"]
