"""Chip registry — reads ``spinor/registry/chips/*.yaml``.

The compiler already owns the registry; here we just expose a Python
view for the API surface ([`GET /api/v1/targets`][jobsvc.routers.targets])
and the cost-control seam.
"""

from __future__ import annotations

import functools
from dataclasses import dataclass
from decimal import Decimal
from pathlib import Path
from typing import Any

import yaml

from .config import get_settings


@dataclass(slots=True, frozen=True)
class ChipInfo:
    """Compact view over a chip YAML.

    Loaded by [`list_chips`][jobsvc.registry.list_chips] from the
    files in ``spinor/registry/chips/``. Hashable + frozen so it can
    be cached by the lru_cache.

    Attributes:
        id: Stable chip identifier (e.g. ``"ibm_heron_r2"``,
            ``"generic"``). Matches the value users pass as
            ``Job.target``.
        provider: Submission backend — one of ``ibm | aws | azure |
            local``. Routes the worker to the right adapter.
        qubits: Number of qubits the chip exposes.
        native_gates: Tuple of native-gate names (e.g.
            ``("ecr", "rz", "sx", "x")``).
        coupling_topology: Topology name (e.g. ``"heavy_hex_156"``,
            ``"all_to_all"``).
        supports: Capability flags (``mid_circuit_measure``,
            ``feedforward``, ``reset``).
        pricing_per_shot_usd: USD per shot. Multiplied by ``shots``
            in [`jobsvc.cost.dollar_cost`][jobsvc.cost.dollar_cost].
        notes: Free-text notes from the YAML.
    """

    id: str
    provider: str
    qubits: int
    native_gates: tuple[str, ...]
    coupling_topology: str
    supports: dict[str, Any]
    pricing_per_shot_usd: Decimal
    notes: str = ""

    def to_public(self) -> dict[str, Any]:
        """Return the JSON shape served at ``GET /api/v1/targets``.

        Drops the ``notes`` field. Coerces ``Decimal`` to ``float``
        because Pydantic's ``TargetOut`` declares the field as
        ``float`` for backwards compatibility with non-Decimal JSON
        consumers.
        """
        return {
            "id": self.id,
            "provider": self.provider,
            "qubits": self.qubits,
            "native_gates": list(self.native_gates),
            "coupling_topology": self.coupling_topology,
            "supports": self.supports,
            "pricing_per_shot_usd": float(self.pricing_per_shot_usd),
        }


@functools.lru_cache(maxsize=1)
def _registry_root() -> Path:
    return Path(get_settings().spinor_registry_root)


def _load_one(path: Path) -> ChipInfo:
    with path.open() as f:
        data = yaml.safe_load(f) or {}
    return ChipInfo(
        id=data["id"],
        provider=data.get("provider", "local"),
        qubits=int(data.get("qubits", 0)),
        native_gates=tuple(data.get("native_gates") or ()),
        coupling_topology=(
            data.get("coupling_map", {}) or {}
        ).get("topology", "unknown"),
        supports=dict(data.get("supports") or {}),
        pricing_per_shot_usd=Decimal(
            str((data.get("pricing") or {}).get("per_shot_usd", 0.0))
        ),
        notes=str(data.get("notes") or ""),
    )


@functools.lru_cache(maxsize=1)
def list_chips() -> list[ChipInfo]:
    """Read every chip YAML under ``$JOBSVC_SPINOR_REGISTRY_ROOT/chips``.

    Cached for the life of the process — call
    [`reset_cache`][jobsvc.registry.reset_cache] in tests that need to
    re-read the registry after writing a new chip YAML.

    Returns:
        Sorted list of [`ChipInfo`][jobsvc.registry.ChipInfo].

    Example:
        >>> [c.id for c in list_chips()]  # doctest: +SKIP
        ['ibm_heron_r2', 'ionq_aria_proto', 'ionq_forte', 'quantinuum_helios']
    """
    root = _registry_root() / "chips"
    if not root.exists():
        return []
    out = [_load_one(p) for p in sorted(root.glob("*.yaml"))]
    return out


def get_chip(chip_id: str) -> ChipInfo | None:
    """Look up a chip by id.

    The synthetic ``"generic"`` target is not a YAML — it's
    materialised here so the API can serve it without a per-test
    fixture.

    Args:
        chip_id: Chip id (matches ``Job.target``).

    Returns:
        [`ChipInfo`][jobsvc.registry.ChipInfo], or ``None`` for unknown
        ids.

    Example:
        >>> g = get_chip("generic")
        >>> g is not None and g.provider == "local"
        True
    """
    for c in list_chips():
        if c.id == chip_id:
            return c
    if chip_id == "generic":
        return ChipInfo(
            id="generic",
            provider="local",
            qubits=64,
            native_gates=("h", "x", "y", "z", "rx", "ry", "rz", "cx", "cz", "swap"),
            coupling_topology="all_to_all",
            supports={"mid_circuit_measure": True, "feedforward": True, "reset": True},
            pricing_per_shot_usd=Decimal("0.0"),
            notes="virtual portable target",
        )
    return None


def reset_cache() -> None:
    """Drop the chip-list cache. Call from tests that mutate the registry."""
    list_chips.cache_clear()
    _registry_root.cache_clear()


__all__ = ["ChipInfo", "get_chip", "list_chips", "reset_cache"]
