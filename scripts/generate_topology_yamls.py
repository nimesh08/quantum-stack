#!/usr/bin/env python3
"""Generate procedural topology YAML files under
``spinor/registry/topologies/``.

Run from the repo root::

    python3 scripts/generate_topology_yamls.py

Topologies emitted:

- square_grid_{120, 84, 9, 20, 54} - rows x cols rectangles (Rigetti
  Ankaa, IBM Nighthawk r1, IQM Garnet/Emerald, OQC).
- heavy_hex_{127, 133, 433} - linear chain + cross-edges, matching
  the procedural style of the existing ``heavy_hex_156``.
- kagome_32 - sparse triangle-chain approximation (OQC Toshiko).

The C++ topology loader at
``spinor/registry/lib/Loader.cpp`` accepts files of the form

    qubits: <N>
    edges: [[a, b], ...]

with ``0 <= a, b < N``, ``a != b``. Procedural layouts are honest:
the upstream layouts will replace these once the calibration ingestion
sweep imports the real per-vendor coupling maps.
"""

from __future__ import annotations

import pathlib
import textwrap

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT_DIR = REPO_ROOT / "spinor" / "registry" / "topologies"


def square_grid(rows: int, cols: int) -> tuple[int, list[tuple[int, int]]]:
    n = rows * cols
    edges: list[tuple[int, int]] = []
    for r in range(rows):
        for c in range(cols):
            i = r * cols + c
            if c + 1 < cols:
                edges.append((i, i + 1))
            if r + 1 < rows:
                edges.append((i, i + cols))
    return n, edges


def heavy_hex_chain(n: int, stride: int) -> tuple[int, list[tuple[int, int]]]:
    edges: list[tuple[int, int]] = [(i, i + 1) for i in range(n - 1)]
    start = 4
    while start + stride < n:
        edges.append((start, start + stride))
        start += stride
    return n, sorted(set(tuple(sorted(e)) for e in edges))


def kagome(n: int) -> tuple[int, list[tuple[int, int]]]:
    edges = [(i, i + 1) for i in range(n - 1)]
    for i in range(0, n - 3, 3):
        edges.append((i, i + 3))
    return n, sorted(set(tuple(sorted(e)) for e in edges))


def fmt_yaml(name: str, header: str, n: int,
             edges: list[tuple[int, int]]) -> str:
    lines = [
        f"# spinor/registry/topologies/{name}.yaml",
        "#",
    ]
    for hl in textwrap.wrap(header, width=68):
        lines.append(f"# {hl}")
    lines.append("")
    lines.append(f"qubits: {n}")
    lines.append("edges: [")
    chunk = []
    for i, (a, b) in enumerate(edges):
        chunk.append(f"[{a},{b}]")
        if (i + 1) % 10 == 0 or i == len(edges) - 1:
            lines.append("  " + ",".join(chunk) + ("," if i != len(edges) - 1 else ""))
            chunk = []
    lines.append("]")
    return "\n".join(lines) + "\n"


SPECS = {
    "square_grid_120": (
        square_grid(12, 10),
        "12x10 rectangular grid (120 qubits). Used by IBM "
        "Nighthawk r1. Procedural; replace with the upstream "
        "edge list from IBM Quantum Platform when available.",
    ),
    "square_grid_84": (
        square_grid(12, 7),
        "12x7 rectangular grid (84 qubits). Approximates the "
        "Rigetti Ankaa-2 / Ankaa-3 lattice. Procedural; replace "
        "with the upstream edge list from Rigetti QCS when "
        "calibration ingestion lands.",
    ),
    "square_grid_9": (
        square_grid(3, 3),
        "3x3 rectangular grid (9 qubits). Used by Rigetti "
        "Ankaa-9Q-3 and similar small dev devices.",
    ),
    "square_grid_20": (
        square_grid(5, 4),
        "5x4 rectangular grid (20 qubits). Approximates IQM "
        "Garnet's 20-qubit square grid. Procedural.",
    ),
    "square_grid_54": (
        square_grid(9, 6),
        "9x6 rectangular grid (54 qubits). Approximates IQM "
        "Emerald's 54-qubit square grid. Procedural.",
    ),
    "heavy_hex_127": (
        heavy_hex_chain(127, 11),
        "127 qubits in a heavy-hex-style sparse graph "
        "(linear chain + stride-11 cross edges). Used by IBM "
        "Brisbane and IBM Sherbrooke (Eagle r3). Procedural; "
        "matches the style of heavy_hex_156.",
    ),
    "heavy_hex_133": (
        heavy_hex_chain(133, 11),
        "133 qubits in a heavy-hex-style sparse graph. Used "
        "by IBM Torrino. Procedural.",
    ),
    "heavy_hex_433": (
        heavy_hex_chain(433, 21),
        "433 qubits in a heavy-hex-style sparse graph. Used "
        "by IBM Osprey (research-class). Procedural.",
    ),
    "kagome_32": (
        kagome(32),
        "32-qubit kagome-lattice approximation (chain plus "
        "every-third cross edge). Used by OQC Toshiko.",
    ),
}


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, (data, header) in SPECS.items():
        n, edges = data
        out = OUT_DIR / f"{name}.yaml"
        out.write_text(fmt_yaml(name, header, n, edges))
        print(f"wrote {out.relative_to(REPO_ROOT)} ({n} qubits, "
              f"{len(edges)} edges)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
