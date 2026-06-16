"""Fixture provider — returns recorded calibration JSON for offline
tests and dev. Always works."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

from .base import CalibrationProvider


class FixtureProvider(CalibrationProvider):
    name = "fixture"

    def fetch(self, chip_id: str) -> dict[str, Any]:
        # Synthetic: deterministic per-chip data the compiler can read.
        return {
            "chip_id": chip_id,
            "fetched_at": datetime.now(timezone.utc).isoformat(),
            "source": "fixture",
            "single_qubit_errors": {str(i): 0.001 + i * 1e-5 for i in range(8)},
            "two_qubit_errors": {f"{i}-{i+1}": 0.01 + i * 1e-4 for i in range(7)},
            "readout_errors": {str(i): 0.02 for i in range(8)},
            "t1_us": {str(i): 100.0 - i for i in range(8)},
            "t2_us": {str(i): 80.0 - i for i in range(8)},
        }


__all__ = ["FixtureProvider"]
