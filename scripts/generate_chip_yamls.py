#!/usr/bin/env python3
"""Generate the 18 Step-1 + 4 Step-2 chip YAML files from a single
spec table. The spec captures only what is independently verifiable
upstream; each YAML's notes block carries the source URL and the
verified-upstream date.

Run::

    python3 scripts/generate_chip_yamls.py
"""

from __future__ import annotations

import pathlib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT_DIR = REPO_ROOT / "spinor" / "registry" / "chips"

VERIFIED = "2026-06-16"


def yaml_block(spec: dict) -> str:
    cid = spec["id"]
    nat = ", ".join(spec["native_gates"])
    cm = spec["coupling_map"]
    if cm["topology"] == "all_to_all":
        cm_lines = (
            "coupling_map:\n"
            "  topology: all_to_all\n"
            f"  size: {cm['size']}\n"
        )
    else:
        cm_lines = (
            "coupling_map:\n"
            f"  topology: {cm['topology']}\n"
            f"  size: {cm['size']}\n"
        )
    sup = spec["supports"]
    feedforward = sup["feedforward"]
    pricing_lines = f"pricing:\n  per_shot_usd: {spec['pricing']['per_shot_usd']}\n"
    if "per_minute_usd" in spec["pricing"]:
        pricing_lines += f"  per_minute_usd: {spec['pricing']['per_minute_usd']}\n"
    pi2 = spec["decomposition"]["one_qubit"].get("pi_2_gate", "")
    pi2_line = f'    pi_2_gate: "{pi2}"' if pi2 == "" else f"    pi_2_gate: {pi2}"
    notes = spec["notes"]

    return (
        f"# spinor/registry/chips/{cid}.yaml\n"
        "\n"
        f"id: {cid}\n"
        f"provider: {spec['provider']}\n"
        f"qubits: {spec['qubits']}\n"
        f"native_gates: [{nat}]\n"
        "\n"
        f"{cm_lines}"
        "\n"
        "supports:\n"
        f"  mid_circuit_measure: {str(sup['mid_circuit_measure']).lower()}\n"
        f"  feedforward: {feedforward}\n"
        f"  reset: {str(sup['reset']).lower()}\n"
        "\n"
        "calibration:\n"
        f"  source: {spec['calibration']['source']}\n"
        f"  refresh: {spec['calibration']['refresh']}\n"
        f"  store: {spec['calibration']['store']}\n"
        "\n"
        "decomposition:\n"
        "  one_qubit:\n"
        "    recipe: euler_zyz\n"
        f"    rotation_gate: {spec['decomposition']['one_qubit']['rotation_gate']}\n"
        f"{pi2_line}\n"
        "  two_qubit:\n"
        "    recipe: kak\n"
        f"    entangler: {spec['decomposition']['two_qubit']['entangler']}\n"
        "    entangler_count_max: 3\n"
        "\n"
        f"{pricing_lines}"
        "\n"
        f"notes: |\n  {notes.rstrip()}\n"
    )


def main() -> int:
    from chip_specs import SPECS  # neighbour module

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for cid, spec in SPECS.items():
        out = OUT_DIR / f"{cid}.yaml"
        out.write_text(yaml_block(spec))
        print(f"wrote {out.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    import sys
    sys.path.insert(0, str(pathlib.Path(__file__).parent))
    raise SystemExit(main())
