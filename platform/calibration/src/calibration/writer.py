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
    """Serialise ``body`` as JSON and atomically replace ``path``.

    Writes to ``<path>.tmp`` then ``os.replace`` so an interrupted
    write never leaves a half-file. The compiler reads the file at
    compile time; correctness of *that* file is what makes nightly
    refresh meaningful.

    Args:
        path: Destination JSON file. Parent directories are created
            if missing (``mkdir -p`` semantics).
        body: JSON-serialisable mapping (the calibration document).

    Returns:
        SHA-256 hex digest of the serialised payload — written to
        [`CalibrationSnapshot.sha256`][jobsvc.models.CalibrationSnapshot]
        so the diff/drift detector can spot identical-content
        re-fetches.

    Example:
        >>> # write_atomic(Path("/tmp/x.json"), {"hello": "world"})
        >>> # 'cf83e1...'
        True
        True
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
    """Read a previous calibration JSON, or ``None`` if missing/invalid.

    Args:
        path: JSON file to read.

    Returns:
        The parsed JSON dict, or ``None`` when the file is missing or
        unparseable. Used by [`refresh_one`][calibration.main.refresh_one]
        to compute drift against the previous fetch.

    Example:
        >>> # read_existing(Path("/tmp/missing.json"))
        >>> # None
        True
        True
    """
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text())
    except (json.JSONDecodeError, OSError):
        return None


__all__ = ["read_existing", "write_atomic"]
