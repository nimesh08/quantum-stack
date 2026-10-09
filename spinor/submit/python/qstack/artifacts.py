"""Content-addressed artifact bundles with explicit format and readout maps."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

from .models import ARTIFACT_VERSION, CompiledArtifact, QStackError
from .registry import canonical_json, digest


def artifact_hash(artifact: CompiledArtifact) -> str:
    # Frozen v1 algorithm: do not add fields or defaults to this preimage.
    identity = {"schema_version": artifact.schema_version, "route": artifact.route, "target": artifact.target, "format": artifact.format,
                   "payload_hash": hashlib.sha256(artifact.program_bytes()).hexdigest(),
                   "physical_ir": artifact.physical_ir, "target_snapshot": artifact.target_snapshot,
                   "compilation": {k: artifact.manifest[k] for k in ("native_spinor", "compiler_version", "source_hash",
                       "optimization_level", "seed", "statistics", "readout_order", "qir_entry_point", "approximation_error_budget") if k in artifact.manifest}}
    if artifact.schema_version == 2:
        identity.update(logical_ir=artifact.logical_ir, numerical_report=artifact.numerical_report,
                        feature_requirements=artifact.feature_requirements,
                        limits=artifact.manifest.get("limits", {}), comparison=artifact.manifest.get("comparison", {}),
                        compiler_tools=artifact.manifest.get("compiler_tools", {}),
                        source_bytes_sha256=artifact.manifest.get("source_bytes_sha256"))
    return digest(identity)


def numerical_evidence(artifact: CompiledArtifact) -> dict:
    """Absent historical evidence is unknown, including intentional budget zero."""
    return artifact.numerical_report if artifact.numerical_report is not None else {
        "schema_version": 1, "status": "unavailable", "certified": False,
        "whole_program_error": None, "coverage": {"complete": False,
        "gaps": ["Numerical evidence unavailable; recompile to collect diagnostics"]}}


def save_artifact(artifact: CompiledArtifact, directory: str | Path) -> Path:
    directory = Path(directory)
    if directory.exists() and any(directory.iterdir()):
        raise QStackError(f"Artifact destination is not empty: {directory}", "OUTPUT_EXISTS")
    directory.mkdir(parents=True, exist_ok=True)
    payload_name = {"qasm3": "program.qasm3", "qasm2": "program.qasm", "qir": "program.bc", "qir-bitcode": "program.bc",
                    "qir-text": "program.ll", "quil": "program.quil", "native-json": "program.json"}.get(artifact.format, "program.bin")
    if artifact.format in {"ionq-native-json", "iqm-json", "anyon-json", "cirq-native", "aqt-native", "qibolab-native", "json"}:
        payload_name = "program.json"
    (directory / payload_name).write_bytes(artifact.program_bytes())
    (directory / "physical.json").write_bytes(canonical_json(artifact.physical_ir))
    (directory / "target.json").write_bytes(canonical_json(artifact.target_snapshot))
    (directory / "optimization.json").write_bytes(canonical_json(artifact.manifest.get("statistics", {})))
    (directory / "mappings.json").write_bytes(canonical_json({
        "measurement_mapping": artifact.physical_ir.get("measurement_mapping", []),
        "logical_to_physical": artifact.physical_ir.get("logical_to_physical", []),
        "initial_logical_to_physical": artifact.physical_ir.get("initial_logical_to_physical", []),
        "readout_order": artifact.manifest.get("readout_order", "c[n-1]...c[0]")}))
    if "native_spinor" in artifact.manifest:
        (directory / "native.spinor").write_text(artifact.manifest["native_spinor"], encoding="utf-8")
    file_names = [payload_name, "physical.json", "target.json", "optimization.json", "mappings.json"]
    if artifact.schema_version == 2:
        if artifact.numerical_report is None:
            artifact.numerical_report = numerical_evidence(artifact)
        extras = {"logical.json": artifact.logical_ir, "numerical.json": numerical_evidence(artifact),
                  "requirements.json": artifact.feature_requirements,
                  "classical.json": {key: artifact.physical_ir.get(key, []) for key in
                                    ("classical_values", "classical_storage", "classical_outputs", "exported_clbits")}}
        for name, value in extras.items():
            (directory / name).write_bytes(canonical_json(value))
        file_names.extend(extras)
    if (directory / "native.spinor").is_file():
        file_names.append("native.spinor")
    manifest = {**artifact.manifest, "schema_version": artifact.schema_version, "route": artifact.route,
                "target": artifact.target, "format": artifact.format, "payload": payload_name,
                "payload_encoding": "binary" if isinstance(artifact.payload, bytes) else "utf-8",
                "artifact_hash": artifact_hash(artifact),
                "files": {name: hashlib.sha256((directory / name).read_bytes()).hexdigest()
                          for name in file_names}}
    (directory / "manifest.json").write_bytes(canonical_json(manifest))
    artifact.path, artifact.manifest = str(directory.resolve()), manifest
    return directory


def load_artifact(directory: str | Path) -> CompiledArtifact:
    directory = Path(directory).resolve()
    try:
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
        version = manifest.get("schema_version")
        if type(version) is not int or version not in {1, ARTIFACT_VERSION}:
            raise QStackError("Unsupported artifact version", "ARTIFACT_INVALID")
        payload_name = manifest["payload"]
        names = set(manifest["files"]) | {payload_name, "physical.json", "target.json"}
        if version == 2:
            names.update(("logical.json", "numerical.json", "requirements.json", "classical.json"))
        for name in names:
            if Path(name).name != name or (directory / name).resolve().parent != directory:
                raise QStackError("Artifact contains an invalid file reference", "ARTIFACT_INVALID")
            if hashlib.sha256((directory / name).read_bytes()).hexdigest() != manifest["files"][name]:
                raise QStackError(f"Artifact file hash mismatch: {name}", "ARTIFACT_INVALID")
        payload = (directory / payload_name).read_bytes()
        if manifest["payload_encoding"] == "utf-8":
            payload = payload.decode("utf-8")
        artifact = CompiledArtifact(manifest["route"], manifest["target"], manifest["format"], payload,
            json.loads((directory / "physical.json").read_bytes()),
            json.loads((directory / "target.json").read_bytes()), manifest, str(directory), schema_version=version,
            logical_ir=json.loads((directory / "logical.json").read_bytes()) if version == 2 else None,
            numerical_report=json.loads((directory / "numerical.json").read_bytes()) if version == 2 else None,
            feature_requirements=json.loads((directory / "requirements.json").read_bytes()) if version == 2 else {})
        if artifact_hash(artifact) != manifest["artifact_hash"]:
            raise QStackError("Artifact manifest hash mismatch", "ARTIFACT_INVALID")
        derived = {
            "optimization.json": artifact.manifest.get("statistics", {}),
            "mappings.json": {
                "measurement_mapping": artifact.physical_ir.get("measurement_mapping", []),
                "logical_to_physical": artifact.physical_ir.get("logical_to_physical", []),
                "initial_logical_to_physical": artifact.physical_ir.get("initial_logical_to_physical", []),
                "readout_order": artifact.manifest.get("readout_order", "c[n-1]...c[0]")},
        }
        if version == 2:
            derived["classical.json"] = {key: artifact.physical_ir.get(key, []) for key in
                                        ("classical_values", "classical_storage", "classical_outputs", "exported_clbits")}
        for name, expected in derived.items():
            if name in manifest["files"] and json.loads((directory / name).read_bytes()) != expected:
                raise QStackError(f"Artifact sidecar disagrees with its hashed core: {name}", "ARTIFACT_INVALID")
        if "native.spinor" in manifest["files"] and (directory / "native.spinor").read_text(encoding="utf-8") != manifest.get("native_spinor"):
            raise QStackError("Artifact native source disagrees with its hashed manifest", "ARTIFACT_INVALID")
        return artifact
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise QStackError("Invalid or incomplete artifact bundle", "ARTIFACT_INVALID") from exc
