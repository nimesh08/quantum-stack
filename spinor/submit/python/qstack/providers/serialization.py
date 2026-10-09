"""Offline provider serialization checks. Never construct a live service client."""
from __future__ import annotations

import io
import json
import re

from qstack.models import QStackError
from .base import optional
from . import submission_objects
from .native import instructions, serialize_native


def validate_qir(artifact):
    sdk = optional("pyqir", "qir")
    try:
        module = (sdk.Module.from_bitcode(sdk.Context(), artifact.program_bytes())
                  if artifact.format.lower() in {"qir", "qir-bitcode", "qir.bc", "qir.v1"}
                  else sdk.Module.from_ir(sdk.Context(), artifact.program_text()))
        error = module.verify()
        if error:
            raise ValueError(error)
    except (ValueError, RuntimeError):
        raise QStackError("Provider QIR payload failed LLVM verification", "INVALID_QIR") from None
    text = str(module)
    entry = artifact.manifest.get("qir_entry_point", "main")
    if not any(function.name == entry and function.basic_blocks for function in module.functions):
        raise QStackError("QIR payload does not define the artifact entry point", "INVALID_QIR")
    platform = artifact.target_snapshot.get("qir_platform", "standard")
    if platform.startswith("quantinuum") and "__quantum__qis__read_result__body" in text:
        raise QStackError("Quantinuum QIR must use its documented runtime read_result", "INVALID_QIR")
    if platform == "standard" and ("__quantum__rt__read_result" in text or "__quantum__qis__rxy__body" in text):
        raise QStackError("Standard QIR cannot use Quantinuum-specific instructions", "INVALID_QIR")
    if platform == "quantinuum-h2" and "__quantum__rt__initialize" in text:
        raise QStackError("H2 does not advertise the Helios initialization runtime", "INVALID_QIR")
    if artifact.route in {"oqc", "alicebob"} and '"qir_profiles"="base_profile"' not in text:
        raise QStackError("This provider's verified QIR route requires base_profile", "UNSUPPORTED_CAPABILITY")
    if artifact.route == "alicebob":
        list(instructions(artifact.physical_ir))
        if platform != "standard":
            raise QStackError("Felis requires standard target QIR", "UNSUPPORTED_FORMAT")
        advertised = {row["signature"].split(":", 1)[0]
            for row in artifact.target_snapshot.get("raw", {}).get("instructions", []) if "signature" in row}
        used = set(re.findall(r"\bcall\s+\w+\s+@(__quantum__qis__\w+)\(", text))
        if advertised and used - advertised:
            raise QStackError("QIR uses instructions absent from the Felis target", "UNSUPPORTED_GATE")
    return {"entry_point": entry, "qir_platform": platform}


def validate_serialization(artifact, config=None):
    """Validate transport serialization using only the artifact and local config.

SDK constructors here are value/schema objects. Authentication, discovery,
submission, transpilation and hardware connection are deliberately absent.
"""
    config = config or {}
    route, fmt = artifact.route, artifact.format.lower()
    summary = {"route": route, "format": fmt, "validated": True, "network_used": False}
    if route == "local":
        summary["serializer"] = "owned-physical-ir"
    elif route == "ibm":
        circuit = submission_objects.ibm_circuit(artifact)
        stream = io.BytesIO()
        optional("qiskit.qpy", "ibm").dump(circuit, stream)
        summary.update(serializer="qiskit-native-qpy", serialized_bytes=len(stream.getvalue()))
    elif route == "google":
        circuit = submission_objects.google_circuit(artifact, config)
        proto = submission_objects.google_protobuf(circuit)
        summary.update(serializer="cirq-engine-protobuf", serialized_bytes=len(proto.SerializeToString()),
                       parameter_precision="float32")
    elif route in {"ionq", "iqm", "anyon"} or (route == "azure" and fmt in {"ionq-native-json", "ionq-json"}):
        native_route = "ionq" if route == "azure" else route
        _, expected = serialize_native(native_route, artifact.physical_ir, artifact.target_snapshot)
        try:
            actual = json.loads(artifact.program_text())
            if actual != json.loads(expected):
                raise ValueError("IR/payload mismatch")
        except ValueError:
            raise QStackError("Native payload differs from serialization of the physical IR", "ARTIFACT_INVALID") from None
        if native_route == "iqm":
            circuit = submission_objects.iqm_circuit(artifact)
            circuit.validate(optional("iqm.pulse.builder", "iqm").build_quantum_ops({}))
        summary["serializer"] = native_route + "-native-json"
    elif route == "aqt":
        body = submission_objects.aqt_body(artifact, shots=config.get("shots", 1))
        operations = body["payload"]["circuits"][0]["quantum_circuit"]
        summary.update(serializer="arnica-native", operation_count=len(operations))
    elif route == "qibolab":
        # Reject extensions before importing or calling a factory: arbitrary
        # bridge code may initialize hardware even while constructing its value.
        if config.get("qibolab_bridge") or config.get("_bridge") is not None:
            raise QStackError("Offline validation requires the built-in qibolab_platform assembler; custom bridge code has no verified offline contract", "UNSUPPORTED_CAPABILITY")
        from .qibolab_native import build_native_sequences, load_platform
        platform = config.get("_platform")
        if platform is None:
            if not config.get("qibolab_platform"):
                raise QStackError("Offline Qibolab validation requires qibolab_platform", "INVALID_CONFIG")
            # A platform definition is trusted laboratory Python configuration.
            # We never call connect/execute, but cannot sandbox its constructor.
            platform = load_platform(config["qibolab_platform"])
        sequences, acquisitions = build_native_sequences(platform, artifact)
        summary.update(serializer="qibolab-calibrated-native", pulse_sequences=len(sequences),
                       acquisitions=len(acquisitions), duration_ns=sequences[0].duration,
                       trusted_platform_configuration=True)
    elif fmt in {"qir", "qir-text", "llvm-ir", "qir.ll", "qir-bitcode", "qir.bc", "qir.v1"} and route in {"quantinuum", "azure", "oqc", "alicebob"}:
        summary.update(serializer="verified-qir", **validate_qir(artifact))
    elif route == "azure" and fmt in {"qasm2", "openqasm2"}:
        from .qasm2 import serialize_qasm2
        if artifact.program_text() != serialize_qasm2(artifact.physical_ir, artifact.target_snapshot):
            raise QStackError("QASM2 differs from the owned native emitter output", "ARTIFACT_INVALID")
        summary["serializer"] = "quantinuum-native-qasm2"
    elif route == "aws":
        list(instructions(artifact.physical_ir))
        text = artifact.program_text()
        if fmt not in {"qasm3", "openqasm3", "braket-openqasm3"} or not text.lstrip().startswith("OPENQASM 3") or "#pragma braket verbatim" not in text:
            raise QStackError("Braket requires owned native OpenQASM3 with a verbatim box", "UNSUPPORTED_FORMAT")
        encoded = submission_objects.aws_program(artifact).json()
        summary.update(serializer="braket-openqasm-program", serialized_bytes=len(encoded.encode()))
    elif route == "rigetti" and fmt in {"quil", "native-quil"}:
        list(instructions(artifact.physical_ir))
        if not artifact.program_text().strip():
            raise QStackError("Native Quil program is empty", "ARTIFACT_INVALID")
        # QCS translate is a remote hardware-control service, not an offline
        # parser. Gate arities/loci are validated in the owned physical IR.
        summary.update(serializer="owned-native-quil", remote_translation_validated=False)
    elif route == "oqc" and fmt in {"qasm2", "openqasm2", "qasm3", "openqasm3"}:
        text = artifact.program_text()
        if not text.lstrip().startswith("OPENQASM " + ("2" if "2" in fmt else "3")):
            raise QStackError("OQC QASM payload has the wrong language version", "ARTIFACT_INVALID")
        task = submission_objects.oqc_task(artifact, shots=config.get("shots", 1))
        task.to_json()
        summary.update(serializer="oqc-task-schema", server_language_validation=False)
    else:
        raise QStackError(f"No verified offline serializer for {route}/{fmt}", "UNSUPPORTED_FORMAT")
    return summary
