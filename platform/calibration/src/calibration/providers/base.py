"""Provider abstract base. Each provider implements `fetch(chip_id) -> dict`
returning a calibration document the compiler reads from
`~/.cache/spinor/calibration/<chip>.json`.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any


class CalibrationProvider(ABC):
    """Abstract base for nightly calibration data sources.

    Subclass to add a new provider; register the subclass in
    [`calibration.providers._REGISTRY`][calibration.providers] and
    update
    [`calibration.main._resolve_provider`][calibration.main]
    to map the chip-YAML ``calibration.source`` value onto your
    provider name.

    The returned document is JSON-serialised by
    [`calibration.writer.write_atomic`][calibration.writer.write_atomic]
    to the path the chip YAML declares (typically
    ``~/.cache/spinor/calibration/<chip>.json``). The compiler reads
    that file at compile time.

    Attributes:
        name: Stable string identifier (e.g. ``"ibm"``,
            ``"fixture"``). Used in
            ``calibration_refresh_total{chip,ok}`` Prometheus metric
            and ``calibration_snapshots.source_provider``.
    """

    name: str = "abstract"

    @abstractmethod
    def fetch(self, chip_id: str) -> dict[str, Any]:
        """Return a JSON-serialisable calibration document.

        Args:
            chip_id: The chip's id (matches the YAML's ``id:`` and
                the ``Job.target`` value).

        Returns:
            Dict with at least these keys:

            - ``chip_id``
            - ``source`` (provider name)
            - ``single_qubit_errors`` — ``{ "<q>": float, ... }``
            - ``two_qubit_errors`` — ``{ "<a>-<b>": float, ... }``
            - ``readout_errors``
            - ``t1_us``, ``t2_us``

        Raises:
            Exception: Any exception is treated as a transient
                failure: the writer keeps the previous file and the
                snapshot row records ``ok=false`` with the error.
        """


__all__ = ["CalibrationProvider"]
