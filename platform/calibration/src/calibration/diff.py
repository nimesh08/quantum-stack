"""Drift detection — flags significant calibration changes."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(slots=True)
class DriftReport:
    """Result of comparing two calibration documents.

    Attributes:
        drifted_qubits: Sorted qubit ids whose single-qubit error
            changed by more than ``relative_threshold``.
        drifted_pairs: Sorted ``"a-b"`` strings whose two-qubit error
            changed by more than ``relative_threshold``.
        relative_threshold: The threshold used for this comparison.
    """

    drifted_qubits: list[str]
    drifted_pairs: list[str]
    relative_threshold: float

    @property
    def has_drift(self) -> bool:
        """True if any qubit or pair drifted past the threshold."""
        return bool(self.drifted_qubits or self.drifted_pairs)


def diff(
    old: dict[str, Any] | None, new: dict[str, Any], *,
    relative_threshold: float = 0.5,
) -> DriftReport:
    """Compute a [`DriftReport`][calibration.diff.DriftReport].

    Compares single- and two-qubit error rates between the previous
    and the freshly-fetched calibration. Anything whose **relative**
    change exceeds ``relative_threshold`` is flagged as drift.

    Args:
        old: Previous calibration dict, or ``None`` (first run).
        new: Newly-fetched calibration dict.
        relative_threshold: Relative change to count as drift, e.g.
            ``0.5`` = 50% (default).

    Returns:
        [`DriftReport`][calibration.diff.DriftReport] with sorted
        ``drifted_qubits`` and ``drifted_pairs`` lists.

    Example:
        >>> diff(None, {"single_qubit_errors": {"0": 0.001}}).has_drift
        False
        >>> r = diff({"single_qubit_errors": {"0": 0.001}, "two_qubit_errors": {}},
        ...          {"single_qubit_errors": {"0": 0.005}, "two_qubit_errors": {}})
        >>> r.has_drift, r.drifted_qubits
        (True, ['0'])
    """
    drifted_q: list[str] = []
    drifted_p: list[str] = []
    if old is None:
        return DriftReport(drifted_qubits=[], drifted_pairs=[],
                           relative_threshold=relative_threshold)

    old_sq = old.get("single_qubit_errors", {}) or {}
    new_sq = new.get("single_qubit_errors", {}) or {}
    for q, ne in new_sq.items():
        oe = old_sq.get(q)
        if oe is None or oe == 0:
            continue
        if abs(ne - oe) / abs(oe) >= relative_threshold:
            drifted_q.append(q)

    old_tq = old.get("two_qubit_errors", {}) or {}
    new_tq = new.get("two_qubit_errors", {}) or {}
    for p, ne in new_tq.items():
        oe = old_tq.get(p)
        if oe is None or oe == 0:
            continue
        if abs(ne - oe) / abs(oe) >= relative_threshold:
            drifted_p.append(p)

    return DriftReport(
        drifted_qubits=sorted(drifted_q),
        drifted_pairs=sorted(drifted_p),
        relative_threshold=relative_threshold,
    )


__all__ = ["DriftReport", "diff"]
