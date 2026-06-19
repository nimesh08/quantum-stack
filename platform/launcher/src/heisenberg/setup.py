"""`heisenberg init` and `heisenberg seed` implementations.

`init` creates the data directory, runs alembic migrations against
the SQLite (or Postgres) URL the launcher is configured for. `seed`
creates a default admin user and prints the issued API key. Both are
idempotent.

Author: Nimesh Cheedella <chnimesh0808@gmail.com>
"""

from __future__ import annotations

import asyncio
import os
from pathlib import Path

from . import paths


def _alembic_ini() -> Path:
    """Locate the jobsvc alembic.ini shipped with the wheel.

    jobsvc's alembic config is at
    ``platform/jobsvc/alembic.ini`` in source builds, and at
    ``<site-packages>/jobsvc/alembic.ini`` in wheel installs once we
    move it inside the package. We currently look for both shapes.
    """
    import jobsvc

    pkg = Path(jobsvc.__file__).resolve().parent
    cand = pkg / "alembic.ini"
    if cand.exists():
        return cand
    src_layout = pkg.parents[1] / "alembic.ini"
    if src_layout.exists():
        return src_layout
    raise FileNotFoundError(
        "could not locate jobsvc alembic.ini; reinstall heisenberg + jobsvc"
    )


def run_migrations() -> None:
    """Run ``alembic upgrade head`` against the configured database."""
    from alembic import command
    from alembic.config import Config

    cfg = Config(str(_alembic_ini()))
    cfg.set_main_option("sqlalchemy.url", os.environ["JOBSVC_DATABASE_URL"])
    command.upgrade(cfg, "head")


def init_data_dir() -> Path:
    d = paths.data_dir()
    paths.calibration_cache_dir()
    config = paths.config_file()
    if not config.exists():
        config.write_text(
            "# Heisenberg config — managed by `heisenberg`.\n"
            "# Override with environment variables (HEISENBERG_*, JOBSVC_*).\n",
            encoding="utf-8",
        )
    return d


async def seed_default_admin(
    email: str = "admin@local",
    password: str = "admin-password",
) -> tuple[str, str]:
    """Create the default admin user + an API key (idempotent).

    Returns:
        ``(email, api_key)``. If the user already exists, ``api_key``
        is the literal string ``"<existing>"`` (we don't expose
        existing keys).
    """
    from sqlalchemy import select
    from jobsvc.auth.api_key import new_api_key
    from jobsvc.auth.passwords import hash_password
    from jobsvc.db import session_scope
    from jobsvc.models import ApiKey, Budget, User, UserRole

    async with session_scope() as session:
        existing = (
            await session.execute(select(User).where(User.email == email))
        ).scalar_one_or_none()
        if existing is not None:
            return email, "<existing>"
        u = User(
            email=email,
            password_hash=hash_password(password),
            role=UserRole.admin,
        )
        session.add(u)
        await session.flush()
        session.add(Budget(user_id=u.id))
        plaintext, prefix, hashed = new_api_key()
        session.add(
            ApiKey(user_id=u.id, prefix=prefix, hash=hashed, label="default")
        )
        await session.commit()
        return email, plaintext


def seed_sync() -> tuple[str, str]:
    return asyncio.run(seed_default_admin())


__all__ = ["init_data_dir", "run_migrations", "seed_default_admin", "seed_sync"]
