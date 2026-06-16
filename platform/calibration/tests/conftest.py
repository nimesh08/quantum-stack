"""Pytest fixtures for the calibration package."""

from __future__ import annotations

from pathlib import Path

import pytest


@pytest.fixture()
def tmp_store(tmp_path: Path) -> Path:
    return tmp_path / "calibration"


@pytest.fixture()
def registry_root(tmp_path: Path) -> Path:
    """A minimal mock registry the loader can read."""
    root = tmp_path / "registry" / "chips"
    root.mkdir(parents=True)
    (root / "fixturechip.yaml").write_text(
        """
id: fixturechip
provider: local
qubits: 4
native_gates: [h, cx]
calibration:
  source: fixture
  refresh: nightly
  store: """ + str(tmp_path / "store" / "fixturechip.json") + """
"""
    )
    (root / "lazy.yaml").write_text(
        """
id: lazy
provider: local
qubits: 2
calibration:
  source: fixture
  refresh: never
  store: """ + str(tmp_path / "store" / "lazy.json") + """
"""
    )
    return tmp_path / "registry"
