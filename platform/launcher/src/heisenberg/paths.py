"""Filesystem layout for the heisenberg launcher.

The data directory holds the SQLite database, calibration cache,
config file, and the launcher's PID file. Defaults to
``$HEISENBERG_DATA_DIR`` if set, else ``$XDG_DATA_HOME/heisenberg``,
else ``~/.local/share/heisenberg``.

Author: Nimesh Cheedella <chnimesh0808@gmail.com>
"""

from __future__ import annotations

import os
from pathlib import Path


def data_dir() -> Path:
    """Resolve and create the heisenberg data directory.

    Resolution order:

    1. ``$HEISENBERG_DATA_DIR`` (explicit override).
    2. ``$XDG_DATA_HOME/heisenberg`` (XDG Base Directory spec).
    3. ``~/.local/share/heisenberg`` (XDG fallback on Linux/macOS).
    """
    if env := os.environ.get("HEISENBERG_DATA_DIR"):
        p = Path(env).expanduser()
    elif xdg := os.environ.get("XDG_DATA_HOME"):
        p = Path(xdg).expanduser() / "heisenberg"
    else:
        p = Path.home() / ".local" / "share" / "heisenberg"
    p.mkdir(parents=True, exist_ok=True)
    return p


def db_path() -> Path:
    return data_dir() / "jobsvc.db"


def calibration_cache_dir() -> Path:
    p = data_dir() / "calibration"
    p.mkdir(parents=True, exist_ok=True)
    return p


def pid_file() -> Path:
    return data_dir() / "heisenberg.pid"


def log_file() -> Path:
    return data_dir() / "heisenberg.log"


def config_file() -> Path:
    return data_dir() / "config.toml"


def default_sqlite_url() -> str:
    """SQLAlchemy URL for the default SQLite database."""
    return f"sqlite+aiosqlite:///{db_path()}"


__all__ = [
    "data_dir",
    "db_path",
    "calibration_cache_dir",
    "pid_file",
    "log_file",
    "config_file",
    "default_sqlite_url",
]
