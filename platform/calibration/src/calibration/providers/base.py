"""Provider abstract base. Each provider implements `fetch(chip_id) -> dict`
returning a calibration document the compiler reads from
`~/.cache/spinor/calibration/<chip>.json`.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any


class CalibrationProvider(ABC):
    name: str = "abstract"

    @abstractmethod
    def fetch(self, chip_id: str) -> dict[str, Any]:
        """Return a JSON-serialisable calibration document. Raises if
        the provider is unreachable or returned malformed data."""


__all__ = ["CalibrationProvider"]
