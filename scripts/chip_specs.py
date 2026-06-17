"""Per-chip spec table consumed by ``generate_chip_yamls.py``.

Verified upstream 2026-06-16. Each entry's ``notes`` records the
canonical source URL. Where a chip is reachable via a cloud provider
we use that provider's published page; where the device is direct-
vendor only we use the vendor's product / datasheet page.
"""

from __future__ import annotations

VERIFIED = "2026-06-16"


def _ibm_eagle(name: str, qubits: int, topology: str,
               url: str, summary: str) -> dict:
    return {
        "id": name,
        "provider": "ibm",
        "qubits": qubits,
        "native_gates": ["ecr", "rz", "sx", "x"],
        "coupling_map": {"topology": topology, "size": qubits},
        "supports": {"mid_circuit_measure": True,
                     "feedforward": "limited", "reset": True},
        "calibration": {"source": "ibm_runtime_api",
                        "refresh": "nightly",
                        "store": f"~/.cache/spinor/calibration/{name}.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "rz", "pi_2_gate": "sx"},
            "two_qubit": {"entangler": "ecr"},
        },
        "pricing": {"per_shot_usd": 0.00016},
        "notes": (
            f"{summary}\n"
            "  Eagle r3 architecture; ECR is the native two-qubit\n"
            "  gate, rz is virtual. Heavy-hex topology.\n"
            f"  source: {url}\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    }


def _ibm_heron(name: str, qubits: int, topology: str,
               url: str, summary: str) -> dict:
    return {
        "id": name,
        "provider": "ibm",
        "qubits": qubits,
        "native_gates": ["cz", "rz", "sx", "x"],
        "coupling_map": {"topology": topology, "size": qubits},
        "supports": {"mid_circuit_measure": True,
                     "feedforward": "limited", "reset": True},
        "calibration": {"source": "ibm_runtime_api",
                        "refresh": "nightly",
                        "store": f"~/.cache/spinor/calibration/{name}.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "rz", "pi_2_gate": "sx"},
            "two_qubit": {"entangler": "cz"},
        },
        "pricing": {"per_shot_usd": 0.00033},
        "notes": (
            f"{summary}\n"
            "  Heron-class tunable-coupler architecture; CZ is the\n"
            "  native two-qubit gate. rz is virtual.\n"
            f"  source: {url}\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    }


def _ionq(name: str, qubits: int, per_shot: float,
          url: str, summary: str, provider: str = "aws") -> dict:
    return {
        "id": name,
        "provider": provider,
        "qubits": qubits,
        "native_gates": ["gpi", "gpi2", "ms"],
        "coupling_map": {"topology": "all_to_all", "size": qubits},
        "supports": {"mid_circuit_measure": False,
                     "feedforward": "none", "reset": False},
        "calibration": {"source": "braket_api",
                        "refresh": "nightly",
                        "store": f"~/.cache/spinor/calibration/{name}.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "gpi", "pi_2_gate": "gpi2"},
            "two_qubit": {"entangler": "ms"},
        },
        "pricing": {"per_shot_usd": per_shot, "per_minute_usd": 0.30},
        "notes": (
            f"{summary}\n"
            "  Trapped-ion all-to-all. Moelmer-Soerensen is the\n"
            "  native two-qubit interaction.\n"
            f"  source: {url}\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    }


def _quantinuum(name: str, qubits: int,
                url: str, summary: str) -> dict:
    return {
        "id": name,
        "provider": "azure",
        "qubits": qubits,
        "native_gates": ["u1q", "rzz"],
        "coupling_map": {"topology": "all_to_all", "size": qubits},
        "supports": {"mid_circuit_measure": True,
                     "feedforward": "full", "reset": True},
        "calibration": {"source": "azure_api",
                        "refresh": "nightly",
                        "store": f"~/.cache/spinor/calibration/{name}.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "u1q", "pi_2_gate": ""},
            "two_qubit": {"entangler": "rzz"},
        },
        "pricing": {"per_shot_usd": 0.05},
        "notes": (
            f"{summary}\n"
            "  Trapped-ion QCCD; full feedforward classical\n"
            "  control. Adaptive-profile QIR target.\n"
            f"  source: {url}\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    }


def _rigetti(name: str, qubits: int, topology: str,
             url: str, summary: str) -> dict:
    return {
        "id": name,
        "provider": "aws",
        "qubits": qubits,
        "native_gates": ["cz", "rx", "rz"],
        "coupling_map": {"topology": topology, "size": qubits},
        "supports": {"mid_circuit_measure": True,
                     "feedforward": "limited", "reset": True},
        "calibration": {"source": "braket_api",
                        "refresh": "nightly",
                        "store": f"~/.cache/spinor/calibration/{name}.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "rz", "pi_2_gate": ""},
            "two_qubit": {"entangler": "cz"},
        },
        "pricing": {"per_shot_usd": 0.00145},
        "notes": (
            f"{summary}\n"
            "  Square-grid superconducting; CZ via tunable couplers.\n"
            "  RX(theta) parametric and RZ virtual.\n"
            f"  source: {url}\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    }


def _iqm(name: str, qubits: int, topology: str,
         url: str, summary: str) -> dict:
    return {
        "id": name,
        "provider": "aws",
        "qubits": qubits,
        "native_gates": ["cz", "rx", "ry", "rz"],
        "coupling_map": {"topology": topology, "size": qubits},
        "supports": {"mid_circuit_measure": True,
                     "feedforward": "limited", "reset": True},
        "calibration": {"source": "braket_api",
                        "refresh": "nightly",
                        "store": f"~/.cache/spinor/calibration/{name}.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "rz", "pi_2_gate": ""},
            "two_qubit": {"entangler": "cz"},
        },
        "pricing": {"per_shot_usd": 0.00145},
        "notes": (
            f"{summary}\n"
            "  IQM superconducting square-grid. Native PRX(theta,phi)\n"
            "  expressed here via {rx, ry, rz}; CZ is the entangler.\n"
            f"  source: {url}\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    }


SPECS = {
    "ibm_heron_r3": _ibm_heron(
        "ibm_heron_r3", 156, "heavy_hex_156",
        "https://quantum.cloud.ibm.com/computers",
        "IBM Heron r3: 156-qubit superconducting (Marrakesh, Fez).",
    ),
    "ibm_brisbane": _ibm_eagle(
        "ibm_brisbane", 127, "heavy_hex_127",
        "https://quantum.cloud.ibm.com/computers",
        "IBM Brisbane (Eagle r3): 127-qubit superconducting.",
    ),
    "ibm_sherbrooke": _ibm_eagle(
        "ibm_sherbrooke", 127, "heavy_hex_127",
        "https://quantum.cloud.ibm.com/computers",
        "IBM Sherbrooke (Eagle r3): 127-qubit superconducting.",
    ),
    "ibm_osprey": _ibm_eagle(
        "ibm_osprey", 433, "heavy_hex_433",
        "https://www.ibm.com/quantum/blog/quantum-roadmap-2033",
        ("IBM Osprey: 433-qubit superconducting research-class. "
         "Not on Premium pay-as-you-go; access via internal/research "
         "channels."),
    ),
    "ibm_nighthawk_r1": {
        "id": "ibm_nighthawk_r1",
        "provider": "ibm",
        "qubits": 120,
        "native_gates": ["cz", "rz", "sx", "x"],
        "coupling_map": {"topology": "square_grid_120", "size": 120},
        "supports": {"mid_circuit_measure": True,
                     "feedforward": "limited", "reset": True},
        "calibration": {"source": "ibm_runtime_api",
                        "refresh": "nightly",
                        "store": "~/.cache/spinor/calibration/ibm_nighthawk_r1.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "rz", "pi_2_gate": "sx"},
            "two_qubit": {"entangler": "cz"},
        },
        "pricing": {"per_shot_usd": 0.00033},
        "notes": (
            "IBM Nighthawk r1: 120-qubit square-grid (12x10).\n"
            "  IBM moved off heavy-hex for Nighthawk; CZ entangler.\n"
            "  source: https://www.ibm.com/quantum/blog/nighthawk\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    },
    "ibm_torrino": _ibm_eagle(
        "ibm_torrino", 133, "heavy_hex_133",
        "https://quantum.cloud.ibm.com/computers",
        "IBM Torrino: 133-qubit Eagle-class.",
    ),
    "quantinuum_h2_1": _quantinuum(
        "quantinuum_h2_1", 56,
        "https://learn.microsoft.com/azure/quantum/provider-quantinuum",
        "Quantinuum H2-1: 56-qubit trapped-ion QCCD.",
    ),
    "quantinuum_h1_1": _quantinuum(
        "quantinuum_h1_1", 20,
        "https://learn.microsoft.com/azure/quantum/provider-quantinuum",
        "Quantinuum H1-1: 20-qubit trapped-ion QCCD.",
    ),
    "ionq_tempo": _ionq(
        "ionq_tempo", 64, 0.03,
        "https://aws.amazon.com/braket/quantum-computers/ionq/",
        "IonQ Tempo: 64-qubit trapped-ion (next-gen Forte family).",
    ),
    "ionq_forte_enterprise": _ionq(
        "ionq_forte_enterprise", 36, 0.0125,
        "https://aws.amazon.com/braket/quantum-computers/ionq/",
        "IonQ Forte Enterprise: 36-qubit trapped-ion.",
    ),
    "ionq_aria_1": _ionq(
        "ionq_aria_1", 25, 0.03,
        "https://aws.amazon.com/braket/quantum-computers/ionq/",
        "IonQ Aria-1: 25-qubit trapped-ion.",
    ),
    "ionq_harmony": _ionq(
        "ionq_harmony", 11, 0.01,
        "https://aws.amazon.com/braket/quantum-computers/ionq/",
        ("IonQ Harmony: 11-qubit trapped-ion. Decommissioning is "
         "underway on AWS Braket; mirror entry exists in "
         "chips_unsupported.md so the cassette path keeps working."),
    ),
    "rigetti_ankaa_3": _rigetti(
        "rigetti_ankaa_3", 82, "square_grid_82",
        "https://aws.amazon.com/braket/quantum-computers/rigetti/",
        "Rigetti Ankaa-3: 82-qubit square-grid superconducting.",
    ),
    "rigetti_ankaa_2": _rigetti(
        "rigetti_ankaa_2", 84, "square_grid_84",
        "https://aws.amazon.com/braket/quantum-computers/rigetti/",
        "Rigetti Ankaa-2: 84-qubit square-grid superconducting.",
    ),
    "rigetti_ankaa_9q_3": _rigetti(
        "rigetti_ankaa_9q_3", 9, "square_grid_9",
        "https://aws.amazon.com/braket/quantum-computers/rigetti/",
        "Rigetti Ankaa-9Q-3: 9-qubit dev device.",
    ),
    "iqm_garnet": _iqm(
        "iqm_garnet", 20, "square_grid_20",
        "https://aws.amazon.com/braket/quantum-computers/iqm/",
        "IQM Garnet: 20-qubit square-grid superconducting.",
    ),
    "iqm_emerald": _iqm(
        "iqm_emerald", 54, "square_grid_54",
        "https://www.meetiqm.com/products/iqm-emerald",
        "IQM Emerald: 54-qubit square-grid superconducting.",
    ),
    "oqc_toshiko": {
        "id": "oqc_toshiko",
        "provider": "aws",
        "qubits": 32,
        "native_gates": ["cz", "rx", "rz"],
        "coupling_map": {"topology": "kagome_32", "size": 32},
        "supports": {"mid_circuit_measure": False,
                     "feedforward": "none", "reset": True},
        "calibration": {"source": "braket_api",
                        "refresh": "nightly",
                        "store": "~/.cache/spinor/calibration/oqc_toshiko.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "rz", "pi_2_gate": ""},
            "two_qubit": {"entangler": "cz"},
        },
        "pricing": {"per_shot_usd": 0.00145},
        "notes": (
            "OQC Toshiko: 32-qubit Kagome-lattice superconducting.\n"
            "  CZ entangler; coaxmon design.\n"
            "  source: https://aws.amazon.com/braket/quantum-computers/oqc/\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    },
    "aqt_pine": {
        "id": "aqt_pine",
        "provider": "local",
        "qubits": 8,
        "native_gates": ["gpi", "gpi2", "ms"],
        "coupling_map": {"topology": "linear_n", "size": 8},
        "supports": {"mid_circuit_measure": False,
                     "feedforward": "none", "reset": False},
        "calibration": {"source": "fixture",
                        "refresh": "fixture",
                        "store": "~/.cache/spinor/calibration/aqt_pine.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "gpi", "pi_2_gate": "gpi2"},
            "two_qubit": {"entangler": "ms"},
        },
        "pricing": {"per_shot_usd": 0.0},
        "notes": (
            "AQT Pine: 8-qubit trapped-ion linear chain (Innsbruck).\n"
            "  Provider 'local' until the AQT API submission adapter\n"
            "  ships; cassette mode covers the round-trip in tests.\n"
            "  source: https://www.aqt.eu/products/\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    },
    "qci_aqumen": {
        "id": "qci_aqumen",
        "provider": "qci",
        "qubits": 8,
        "native_gates": ["cz", "rx", "rz"],
        "coupling_map": {"topology": "linear_n", "size": 8},
        "supports": {"mid_circuit_measure": True,
                     "feedforward": "limited", "reset": True},
        "calibration": {"source": "fixture",
                        "refresh": "fixture",
                        "store": "~/.cache/spinor/calibration/qci_aqumen.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "rz", "pi_2_gate": ""},
            "two_qubit": {"entangler": "cz"},
        },
        "pricing": {"per_shot_usd": 0.0},
        "notes": (
            "Quantum Circuits Inc. (QCI) Aqumen-class dual-rail\n"
            "  superconducting (Schoelkopf group, Yale). 8-qubit\n"
            "  prototype. Production submission adapter is wired but\n"
            "  the live-mode body raises until the public REST URL\n"
            "  is published; see chips_unsupported.md for what is\n"
            "  missing.\n"
            "  source: https://quantumcircuits.com/products\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    },
    "anyon_yukon": {
        "id": "anyon_yukon",
        "provider": "anyon",
        "qubits": 8,
        "native_gates": ["cz", "rx", "rz"],
        "coupling_map": {"topology": "linear_n", "size": 8},
        "supports": {"mid_circuit_measure": False,
                     "feedforward": "none", "reset": True},
        "calibration": {"source": "fixture",
                        "refresh": "fixture",
                        "store": "~/.cache/spinor/calibration/anyon_yukon.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "rz", "pi_2_gate": ""},
            "two_qubit": {"entangler": "cz"},
        },
        "pricing": {"per_shot_usd": 0.0},
        "notes": (
            "Anyon Technologies Yukon: 8-qubit superconducting on\n"
            "  Anyon's first-gen QPU (Toronto). Cassette-only until\n"
            "  the production REST endpoint is published; see\n"
            "  chips_unsupported.md.\n"
            "  source: https://anyontech.com/yukon/\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    },
    "tii_falcon": {
        "id": "tii_falcon",
        "provider": "tii",
        "qubits": 5,
        "native_gates": ["cz", "rx", "rz"],
        "coupling_map": {"topology": "linear_n", "size": 5},
        "supports": {"mid_circuit_measure": False,
                     "feedforward": "none", "reset": True},
        "calibration": {"source": "fixture",
                        "refresh": "fixture",
                        "store": "~/.cache/spinor/calibration/tii_falcon.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "rz", "pi_2_gate": ""},
            "two_qubit": {"entangler": "cz"},
        },
        "pricing": {"per_shot_usd": 0.0},
        "notes": (
            "TII (Technology Innovation Institute, UAE) Falcon\n"
            "  prototype: 5-qubit superconducting referenced in the\n"
            "  Qibolab open-source drivers. Cassette-only; the live\n"
            "  endpoint runs on the user's own Qibolab service.\n"
            "  source: https://github.com/qiboteam/qibolab\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    },
    "alicebob_boson_4": {
        "id": "alicebob_boson_4",
        "provider": "alicebob",
        "qubits": 4,
        "native_gates": ["x", "z", "cx"],
        "coupling_map": {"topology": "linear_n", "size": 4},
        "supports": {"mid_circuit_measure": True,
                     "feedforward": "limited", "reset": True},
        "calibration": {"source": "fixture",
                        "refresh": "fixture",
                        "store": "~/.cache/spinor/calibration/alicebob_boson_4.json"},
        "decomposition": {
            "one_qubit": {"rotation_gate": "rz", "pi_2_gate": ""},
            "two_qubit": {"entangler": "cx"},
        },
        "pricing": {"per_shot_usd": 0.0},
        "notes": (
            "Alice & Bob Boson 4: 4 logical cat-qubits with\n"
            "  bias-preserving X/Z/CX. Shipped via the Alice & Bob\n"
            "  cloud (preview) and a public emulator. Cassette-only\n"
            "  until the production submission URL is published.\n"
            "  source: https://alice-bob.com/products/cloud/\n"
            f"  verified-upstream: {VERIFIED}\n"
        ),
    },
}
