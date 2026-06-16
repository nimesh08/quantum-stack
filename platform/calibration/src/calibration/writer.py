"""Atomic JSON writer for the calibration store.

Writes via `tmpfile + os.replace` so an interrupted write never leaves
a half-file. The compiler reads the file at compile time; correctness
of *that* file is what makes nightly refresh meaningful.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
from typing import Any


def write_atomic(path: Path, body: dict[str, Any]) -> str:
    """Serialise `body` as JSON and atomically replace `path`.

    Returns the SHA-256 of the serialized payload.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(body, indent=2, sort_keys=True).encode("utf-8")
    digest = hashlib.sha256(payload).hexdigest()
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "wb") as f:
        f.write(payload)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)
    return digest


def read_existing(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text())
    except (json.JSONDecodeError, OSError):
        return None


__all__ = ["read_existing", "write_atomic"]
