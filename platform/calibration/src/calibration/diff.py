"""Drift detection — flags significant calibration changes."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(slots=True)
class DriftReport:
    drifted_qubits: list[str]
    drifted_pairs: list[str]
    relative_threshold: float

    @property
    def has_drift(self) -> bool:
        return bool(self.drifted_qubits or self.drifted_pairs)


def diff(
    old: dict[str, Any] | None, new: dict[str, Any], *,
    relative_threshold: float = 0.5,
) -> DriftReport:
    """Compare single- and two-qubit error rates; return the qubits/pairs
    where the relative change exceeds `relative_threshold`."""
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
