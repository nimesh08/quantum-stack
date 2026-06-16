"""Azure calibration — stub at v1 (D5)."""

from __future__ import annotations

from typing import Any

from .base import CalibrationProvider


class AzureProvider(CalibrationProvider):
    name = "azure"

    def fetch(self, chip_id: str) -> dict[str, Any]:
        raise NotImplementedError(
            "Azure Quantum calibration provider is a v1 stub (D5). "
            "Implement using azure.quantum.Workspace.get_targets."
        )


__all__ = ["AzureProvider"]
