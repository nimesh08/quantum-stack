"""Content-addressed artifact bundles with explicit format and readout maps."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

from .models import CompiledArtifact, QStackError, SCHEMA_VERSION
from .registry import canonical_json, digest


def artifact_hash(artifact: CompiledArtifact) -> str:
    return digest({"schema_version": artifact.schema_version, "route": artifact.route, "target": artifact.target, "format": artifact.format,
                   "payload_hash": hashlib.sha256(artifact.program_bytes()).hexdigest(),
                   "physical_ir": artifact.physical_ir, "target_snapshot": artifact.target_snapshot,
                   "compilation": {k: artifact.manifest[k] for k in ("native_spinor", "compiler_version", "source_hash",
                       "optimization_level", "seed", "statistics", "readout_order", "qir_entry_point", "approximation_error_budget") if k in artifact.manifest}})


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
    if (directory / "native.spinor").is_file():
        file_names.append("native.spinor")
    manifest = {**artifact.manifest, "schema_version": SCHEMA_VERSION, "route": artifact.route,
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
        if manifest.get("schema_version") != SCHEMA_VERSION:
            raise QStackError("Unsupported artifact version", "ARTIFACT_INVALID")
        payload_name = manifest["payload"]
        names = set(manifest["files"]) | {payload_name, "physical.json", "target.json"}
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
            json.loads((directory / "target.json").read_bytes()), manifest, str(directory))
        if artifact_hash(artifact) != manifest["artifact_hash"]:
            raise QStackError("Artifact manifest hash mismatch", "ARTIFACT_INVALID")
        return artifact
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise QStackError("Invalid or incomplete artifact bundle", "ARTIFACT_INVALID") from exc
