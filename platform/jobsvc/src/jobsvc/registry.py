"""Chip registry — reads `spinor/registry/chips/*.yaml`.

The compiler already owns the registry; here we just expose a Python
view for the API surface (`GET /targets`) and the cost-control seam.
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
    id: str
    provider: str
    qubits: int
    native_gates: tuple[str, ...]
    coupling_topology: str
    supports: dict[str, Any]
    pricing_per_shot_usd: Decimal
    notes: str = ""

    def to_public(self) -> dict[str, Any]:
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
    root = _registry_root() / "chips"
    if not root.exists():
        return []
    out = [_load_one(p) for p in sorted(root.glob("*.yaml"))]
    return out


def get_chip(chip_id: str) -> ChipInfo | None:
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
    list_chips.cache_clear()
    _registry_root.cache_clear()


__all__ = ["ChipInfo", "get_chip", "list_chips", "reset_cache"]
