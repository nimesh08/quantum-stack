"""Owned native-gate to calibrated Qibolab pulse assembly.

Only the platform's documented native definitions are used. No Qibo circuit,
Qibolab compiler, calibration synthesis, or hardware connection occurs here.
"""
from __future__ import annotations

import importlib.util
import json
from math import pi
from pathlib import Path
from uuid import uuid4

from qstack.models import QStackError
from .base import optional
from .capability import calibration_digest
from .native import instructions


def load_platform(name):
    sdk = optional("qibolab", "qibolab")
    directory = Path(name).expanduser()
    if directory.is_dir():
        source = directory / "platform.py"
        if not source.is_file():
            raise QStackError("qibolab_platform directory must contain platform.py", "INVALID_CONFIG")
        # Match the documented platform.py:create() convention, with an isolated
        # module name. No global QIBOLAB_PLATFORMS environment change is needed.
        spec = importlib.util.spec_from_file_location("qstack_platform_" + uuid4().hex, source)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        hardware = module.create()
        if isinstance(hardware, sdk.Platform):
            return hardware
        if not isinstance(hardware, sdk.Hardware):
            raise QStackError("platform.py:create() must return Qibolab Hardware or Platform", "INVALID_CONFIG")
        return sdk.Platform.load(directory, **vars(hardware))
    return sdk.create_platform(name)


def platform_snapshot(platform):
    identifiers = list(platform.qubits)
    index = {name: i for i, name in enumerate(identifiers)}
    loci, durations = {}, []
    def add(gate, qs, duration=None):
        loci.setdefault(gate, []).append(qs)
        if duration is not None:
            durations.append({"op": gate, "qubits": qs, "duration_ns": float(duration)})
    for name, definitions in platform.natives.single_qubit.items():
        if name not in index:
            continue
        q = index[name]
        if definitions.RX is not None or definitions.RX90 is not None:
            add("u1q", [q])  # duration can depend on theta when RX90 is used
        if platform.qubits[name].drive is not None:
            add("rz", [q], 0)
        if definitions.MZ is not None:
            add("measure", [q], definitions.MZ.duration)
    edges = set()
    for pair, definitions in platform.natives.two_qubit.items():
        if not all(name in index for name in pair):
            continue
        qs = [index[name] for name in pair]
        for gate, native, symmetric in (("cz", "CZ", True), ("cx", "CNOT", False), ("iswap", "iSWAP", True)):
            definition = getattr(definitions, native)
            if definition is not None:
                add(gate, qs, definition.duration)
                edges.add(tuple(qs))
                if symmetric:
                    add(gate, qs[::-1], definition.duration)
                    edges.add(tuple(qs[::-1]))
    dummy = any(type(instrument).__module__.startswith(("qibolab._core.dummy", "qibolab._core.instruments.dummy")) for instrument in platform.instruments.values())
    return {"route": "qibolab", "vendor": "configured-lab", "device": platform.name,
        "qubits": len(identifiers), "qubit_labels": [str(i) for i in identifiers], "qubit_ids": identifiers,
        "native_gates": sorted(loci), "gate_loci": loci, "coupling": sorted(map(list, edges)),
        "all_to_all": False, "directed_connectivity": True, "parameter_units": "radians",
        "formats": ["qibolab-native"], "supports": {"reset": False, "mid_circuit_measure": False, "feedforward": False},
        "instruction_durations": durations, "calibration_sha256": calibration_digest(json.loads(platform.parameters.model_dump_json())),
        "capability_verified": bool(identifiers and loci) and not dummy,
        "readiness": "offline_only" if dummy else "configured_lab",
        "readiness_reason": "Qibolab dummy controller generates synthetic test data" if dummy else None,
        "capability_sources": ["configured Qibolab platform native pulse definitions",
            "https://qibo.science/qibolab/stable/main-documentation/platform.html"]}


def native_plan(artifact):
    """Pure ordered input to the calibrated assembler, with physical identities.

    This plan is inspectable offline. Its ideal gate semantics do not establish
    the unitary or fidelity of any laboratory's pulse calibration.
    """
    ids = artifact.target_snapshot.get("qubit_ids")
    if ids is None or len(ids) < artifact.physical_ir["num_qubits"]:
        raise QStackError("Qibolab verification requires recorded qubit_ids; refresh and recompile", "MISSING_TARGET_SNAPSHOT")
    plan, measured = [], False
    arities = {"u1q": (1, 2), "rz": (1, 1), "cz": (2, 0), "cx": (2, 0),
               "iswap": (2, 0), "measure": (1, 0), "gphase": (0, 1)}
    for position, item in enumerate(instructions(artifact.physical_ir)):
        op, qs, params = item["op"], item.get("qubits", []), item.get("params", [])
        if op != "barrier" and (op not in arities or (len(qs), len(params)) != arities[op]):
            raise QStackError(f"Built-in Qibolab assembler cannot encode '{op}'", "UNSUPPORTED_GATE")
        if any(type(q) is not int or not 0 <= q < len(ids) for q in qs):
            raise QStackError("Qibolab instruction refers to an unknown physical qubit", "ARTIFACT_INVALID")
        if measured and op not in {"measure", "barrier", "gphase"}:
            raise QStackError("Built-in Qibolab pulse assembly supports terminal readout only", "UNSUPPORTED_CAPABILITY")
        bit = None
        if op == "measure":
            bits = item.get("clbits", [])
            if len(bits) != 1 or type(bits[0]) is not int or not 0 <= bits[0] < artifact.physical_ir["num_clbits"]:
                raise QStackError("Qibolab readout requires one valid classical destination", "ARTIFACT_INVALID")
            bit, measured = bits[0], True
        plan.append({"position": position, "operation": op, "physical_ids": [ids[q] for q in qs],
                     "parameters": list(params), "readout": bit})
    return plan


def build_native_sequences(platform, artifact):
    sdk = optional("qibolab", "qibolab")
    current = platform_snapshot(platform)
    snapshot = artifact.target_snapshot
    if artifact.target != current["device"]:
        raise QStackError("Artifact target differs from the configured Qibolab platform", "TARGET_INCOMPATIBLE")
    for key in ("calibration_sha256", "qubit_ids", "qubit_labels"):
        if key in snapshot and snapshot[key] != current[key]:
            raise QStackError("Qibolab calibration or physical qubit identifiers changed; refresh and recompile", "TARGET_INCOMPATIBLE")
    from qstack.registry import validate_physical
    validate_physical(artifact.physical_ir, current)
    ids = current["qubit_ids"]
    layers = {i["instruction"]: i["layer"] for i in artifact.manifest.get("statistics", {}).get("schedule", [])}
    sequence, parallel, previous_layer = sdk.PulseSequence(), sdk.PulseSequence(), None
    acquisitions = []
    measured = False
    # Supply the same concrete IDs to older artifacts that predate snapshots.
    from dataclasses import replace
    planned_artifact = replace(artifact, target_snapshot={**snapshot, "qubit_ids": ids})
    for inst in native_plan(planned_artifact):
        position, op, params = inst["position"], inst["operation"], inst["parameters"]
        native_ids = inst["physical_ids"]
        qs = [ids.index(identifier) for identifier in native_ids]
        if op == "gphase":
            continue  # scalar phase remains in the physical artifact
        if op == "barrier":
            if parallel:
                sequence |= parallel
                parallel = sdk.PulseSequence()
            previous_layer = None
            continue
        if measured and op != "measure":
            raise QStackError("Built-in Qibolab pulse assembly supports terminal readout only", "UNSUPPORTED_CAPABILITY")
        if op == "u1q":
            # R's documented input is [0,2*pi). Period reduction changes only
            # a scalar phase, which the physical artifact already preserves.
            part = platform.natives.single_qubit[native_ids[0]].R(theta=params[0] % (2 * pi), phi=params[1])
        elif op == "rz":
            part = sdk.PulseSequence([(platform.qubits[native_ids[0]].drive, sdk.VirtualZ(phase=-params[0]))])
        elif op in {"cz", "cx", "iswap"}:
            pair = tuple(native_ids)
            definition = platform.natives.two_qubit.get(pair)
            if definition is None and op in {"cz", "iswap"}:
                definition = platform.natives.two_qubit.get(pair[::-1])
            part = definition.ensure({"cz": "CZ", "cx": "CNOT", "iswap": "iSWAP"}[op]).create_sequence()
        elif op == "measure":
            measured = True
            part = platform.natives.single_qubit[native_ids[0]].ensure("MZ").create_sequence()
            if len(part.acquisitions) != 1:
                raise QStackError("Each calibrated MZ must expose one discriminated acquisition", "UNSUPPORTED_CAPABILITY")
            acquisitions.append({"id": str(part.acquisitions[0][1].id), "qubit": qs[0], "clbit": inst["readout"]})
        else:
            raise QStackError(f"Built-in Qibolab assembler cannot encode '{op}'", "UNSUPPORTED_GATE")
        layer = layers.get(position, position)
        # Honor owned parallel layers only when their calibrated pulse channels
        # are disjoint. Shared control/readout channels serialize conservatively.
        if parallel and (layer != previous_layer or parallel.channels.intersection(part.channels)):
            sequence |= parallel
            parallel = sdk.PulseSequence()
        parallel.extend(part)
        previous_layer = layer
    if parallel:
        sequence |= parallel
    return [sequence.align_to_delays()], acquisitions


def acquisition_counts(raw, mapping, num_clbits, shots):
    if not mapping:
        return None
    columns = {}
    for acquisition in mapping:
        values = raw.get(str(acquisition["id"]))
        if not isinstance(values, list) or len(values) != shots:
            return None
        bits = []
        for value in values:
            while isinstance(value, list) and len(value) == 1:
                value = value[0]
            if value not in (0, 1) or isinstance(value, (list, dict)):
                return None
            bits.append(int(value))
        columns[acquisition["clbit"]] = bits
    counts = {}
    for shot in range(shots):
        bitstring = "".join(str(columns[bit][shot] if bit in columns else 0) for bit in reversed(range(num_clbits)))
        counts[bitstring] = counts.get(bitstring, 0) + 1
    return counts
