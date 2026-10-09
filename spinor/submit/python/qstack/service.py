"""Compile once in C++; serialize, authenticate and execute through explicit routes."""
from __future__ import annotations

import json
import hashlib
import math
import os
import shutil
import subprocess
import tempfile
import time
import uuid
from pathlib import Path

import yaml

from . import __version__
from .artifacts import artifact_hash, save_artifact
from .config import SECRET_FIELDS
from .jobs import read_with_retry, save_job, wait_for_result
from .models import ARTIFACT_VERSION, CompiledArtifact, ExecutionResult, JobReceipt, QStackError, SubmissionOptions
from .registry import canonical_format, digest, ensure_live_target, get_target, registry_root, target_edges, validate_format, validate_physical
from .scheduling import optimization_report


def find_binary(name: str, config: dict | None = None) -> str:
    explicit = (config or {}).get(name) or os.environ.get("QSTACK_" + name.upper())
    if explicit:
        path = Path(explicit).expanduser()
        if path.is_file():
            return str(path.resolve())
        raise QStackError(f"Configured compiler does not exist: {name}", "COMPILER_MISSING")
    found = shutil.which(name)
    if found:
        return found
    for parent in Path(__file__).resolve().parents:
        phase = {"photonc": "photon", "phononc": "phonon", "spinorc": "spinor"}.get(name)
        if phase:
            base = parent / "build" / phase / "cli"
            for directory in (base, *(base / mode for mode in ("Release", "RelWithDebInfo", "Debug"))):
                for suffix in (".exe", ""):
                    candidate = directory / (name + suffix)
                    if candidate.is_file():
                        return str(candidate)
    raise QStackError(f"Compiler '{name}' was not found; install the core CLI or set QSTACK_{name.upper()}", "COMPILER_MISSING")


def _process(args: list[str], env: dict) -> str:
    try:
        proc = subprocess.run(args, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
    except OSError as exc:
        raise QStackError(f"Could not launch compiler {Path(args[0]).name}", "COMPILER_MISSING") from exc
    if proc.returncode:
        raise QStackError((proc.stderr or proc.stdout or "Compiler failed").strip(), "COMPILATION_FAILED")
    return proc.stdout


def _compiler_registry(record: dict, config: dict, scratch: Path) -> tuple[Path, str]:
    if not record.get("capability_verified") and not any(k.startswith("placement_") for k in config):
        return registry_root(config), record["id"]
    root = scratch / "registry"
    (root / "chips").mkdir(parents=True)
    (root / "topologies").mkdir()
    compiler_id = "device_" + digest([record.get("route"), record["device"]])[:16]
    gates = [g for g in record["native_gates"] if g not in {"measure", "reset", "barrier", "delay", "if_else", "switch_case", "for_loop", "while_loop"}]
    entangler = next((g for g in ("cz", "cx", "rzz", "rxx", "ms", "ecr", "sqrt_iswap", "sqrt_iswap_inv", "syc", "iswap") if g in gates), "")
    supports = dict(record.get("supports", {}))
    if isinstance(supports.get("feedforward"), bool):
        supports["feedforward"] = "full" if supports["feedforward"] else "none"
    all_to_all, edges = target_edges(record, config)
    if entangler in record.get("gate_loci", {}):
        # Connectivity is only a search graph. Per-operation ordered loci below
        # determine which actual recipes and SWAP decompositions are legal.
        edges = sorted({tuple(locus) for loci in record["gate_loci"].values() for locus in loci if len(locus) == 2})
        edges = [list(edge) for edge in edges]
        all_to_all = False
    chip = {"id": compiler_id, "provider": record["route"], "vendor": record.get("vendor", ""),
            "qir_platform": record.get("qir_platform", "standard"),
            "qubits": record["qubits"], "native_gates": gates, "supports": supports,
            "coupling_map": {"topology": "discovered", "size": record["qubits"]},
            "directed_connectivity": record.get("directed_connectivity", False),
            "calibration": record.get("calibration", {k: record[k] for k in
                ("one_qubit_errors", "readout_errors", "two_qubit_errors", "instruction_durations") if k in record}),
            "decomposition": {"one_qubit": {"recipe": "euler_zyz", "rotation_gate": "rz",
                "pi_2_gate": "sx" if "sx" in gates else "gpi2" if "gpi2" in gates else "rx" if "rx" in gates else ""},
                "two_qubit": {"recipe": "kak", "entangler": entangler, "entangler_count_max": 3}}}
    if record.get("capabilities"):
        chip["capabilities"] = record["capabilities"]
    if record.get("resonator_qubits"):
        chip.update(computational_qubits=record["computational_qubits"],
                    resonator_qubits=record["resonator_qubits"],
                    move_loci=record.get("gate_loci", {}).get("move", []),
                    cz_loci=record.get("gate_loci", {}).get("cz", []))
    for key in ("available_qubits", "unavailable_qubits"):
        if key in record:
            chip[key] = record[key]
    single_qubit_gates = {"h", "x", "y", "z", "s", "sdg", "t", "tdg", "sx", "sxdg",
                          "rx", "ry", "rz", "u1q", "gpi", "gpi2", "phased_xz", "measure", "reset"}
    loci = {gate: [locus[0] for locus in values] for gate, values in record.get("gate_loci", {}).items()
            if gate in single_qubit_gates and all(len(locus) == 1 for locus in values)}
    if loci:
        chip["single_qubit_gate_loci"] = loci
    two_qubit_loci = {gate: values for gate, values in record.get("gate_loci", {}).items()
                      if values and all(len(locus) == 2 for locus in values)}
    if two_qubit_loci:
        chip["two_qubit_gate_loci"] = two_qubit_loci
    chip["placement"] = {"seed": config.get("seed", 42), **{
        key.removeprefix("placement_"): value for key, value in config.items() if key.startswith("placement_")}}
    # The small C++ reader accepts block maps and flow lists, not flow maps.
    # Timing objects belong in the Python scheduling report; only numeric error
    # pairs/triples are passed to the native placement engine.
    chip["calibration"] = {k: v for k, v in chip["calibration"].items()
                           if k in {"one_qubit_errors", "readout_errors", "two_qubit_errors"}}
    class RegistryDumper(yaml.SafeDumper):
        def ignore_aliases(self, data):
            # The standalone registry reader deliberately has no YAML aliases.
            # IQM MOVE/CZ lists occur in both component and operation contracts.
            return True
    RegistryDumper.add_representer(list, lambda dumper, value: dumper.represent_sequence("tag:yaml.org,2002:seq", value, flow_style=True))
    def registry_yaml(data):
        return yaml.dump(data, Dumper=RegistryDumper, default_flow_style=False, sort_keys=False, width=100000)
    if not chip["calibration"]:
        del chip["calibration"]
    (root / "chips" / (compiler_id + ".yaml")).write_text(registry_yaml(chip), encoding="utf-8")
    (root / "topologies" / "discovered.yaml").write_text(
        registry_yaml({"qubits": record["qubits"], "all_to_all": all_to_all, "edges": edges,
                       "directed": record.get("directed_connectivity", False)}), encoding="utf-8")
    return root, compiler_id


def compile_file(source: str | Path, *, target: str, config: dict | None = None,
                 output: str | Path | None = None, language: str | None = None,
                 optimization_level: int = 2, format: str | None = None) -> CompiledArtifact:
    started = time.perf_counter()
    config = dict(config or {})
    route = config.get("provider")
    record = get_target(target, route, config)
    if config.get("timing_model"):
        from .target_models import attach_timing_model
        record = attach_timing_model(record, config["timing_model"])
    route = route or record["provider"]
    source = Path(source).resolve()
    if not source.is_file():
        raise QStackError(f"Source file not found: {source}")
    source_bytes = source.read_bytes()
    source_text = source_bytes.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")
    if optimization_level not in range(4):
        raise QStackError("optimization level must be 0, 1, 2 or 3")
    language = language or {".pho": "photon", ".phn": "phonon", ".phonon": "phonon",
                            ".spn": "spinor", ".spinor": "spinor", ".qasm": "qasm", ".qasm3": "qasm"}.get(source.suffix.lower())
    if language not in {"photon", "phonon", "spinor", "qasm"}:
        raise QStackError("Unknown source language; specify --language photon|phonon|spinor|qasm")
    sc = find_binary("spinorc", config)
    driver = find_binary("photonc" if language == "photon" else "phononc", config) if language in {"photon", "phonon"} else sc
    def compiler_identity():
        result = {}
        for path in sorted({sc, driver}):
            with open(path, "rb") as executable:
                result[Path(path).stem] = {"sha256": hashlib.file_digest(executable, "sha256").hexdigest(),
                                           "size_bytes": Path(path).stat().st_size}
        return result
    compiler_tools = compiler_identity()
    env = dict(os.environ)
    env["QSTACK_SPINORC"] = sc
    env.pop("QSTACK_FORWARDED_ARGS", None)
    with tempfile.TemporaryDirectory(prefix="qstack-") as temp:
        scratch = Path(temp)
        report_path, logical_path = scratch / "numerical.json", scratch / "logical.json"
        env["QSTACK_NUMERICAL_REPORT"] = str(report_path)
        env["QSTACK_LOGICAL_IR_OUTPUT"] = str(logical_path)
        env["QSTACK_EXPANDED_OPERATION_BUDGET"] = str(config.get("expanded_operation_budget", 100000))
        root, compiler_id = _compiler_registry(record, config, scratch)
        env["SPINOR_REGISTRY_ROOT"] = str(root)
        if language in {"photon", "phonon"}:
            native = _process([driver, "compile", "--target", compiler_id, "--emit", "spinor", "-O", str(optimization_level), str(source)], env)
        else:
            native = _process([sc, "compile", "-t", compiler_id, "-O", str(optimization_level), str(source)], env)
        native_path = scratch / "native.spinor"
        native_path.write_text(native, encoding="utf-8")
        def emit(fmt):
            extra = ["--verbatim"] if route == "aws" and fmt == "qasm3" else []
            return _process([sc, "emit", "--compiled", "-t", compiler_id, "-f", fmt, *extra, str(native_path)], env)
        physical = json.loads(emit("json"))
        physical["target"] = target
        if not logical_path.is_file() or not report_path.is_file():
            raise QStackError("Compiler did not produce v2 logical/numerical evidence; rebuild all compiler drivers", "COMPILER_VERSION_MISMATCH")
        logical, numerical = json.loads(logical_path.read_bytes()), json.loads(report_path.read_bytes())
        from .classical import extract_requirements, validate_classical, needs_controller_contract
        validate_classical(physical)
        requirements = extract_requirements(physical)
        numerical["logical_ir_hash"] = digest(logical)
        numerical["physical_ir_hash"] = digest(physical)
        validate_physical(physical, record, config)
        if format:
            chosen = canonical_format(format)
        else:
            chosen = {"ionq": "ionq-native-json", "iqm": "iqm-json", "anyon": "anyon-json",
                      "rigetti": "quil", "quantinuum": "qir-bitcode", "azure": "qir-bitcode",
                      "alicebob": "qir-text", "google": "cirq-native", "aqt": "aqt-native",
                      "qibolab": "qibolab-native"}.get(route, "qasm3")
            if route == "azure" and "ionq-native-json" in record.get("formats", []):
                chosen = "ionq-native-json"
            if route == "oqc" and record.get("capability_verified"):
                formats = {canonical_format(f) for f in record.get("formats", [])}
                chosen = next((f for f in ("qasm3", "qir-bitcode", "qir-text", "qasm2") if f in formats), chosen)
        if record.get("capability_verified"):
            validate_format(route, chosen, record)
        if needs_controller_contract(requirements):
            from .classical import validate_requirements, serializer_capabilities
            validate_requirements(requirements, record, serializer_capabilities(chosen))
        if chosen in {"ionq-native-json", "iqm-json", "anyon-json"}:
            from .providers.native import serialize_native
            serializer_route = "ionq" if chosen == "ionq-native-json" else route
            chosen, payload = serialize_native(serializer_route, physical, record)
        elif chosen in {"cirq-native", "aqt-native", "qibolab-native"}:
            payload = json.dumps(physical)
        elif chosen == "qasm2":
            from .providers.qasm2 import serialize_qasm2
            payload = serialize_qasm2(physical, record)
        elif chosen in {"qir", "qir-text", "qir-bitcode"}:
            payload = emit("qir")
            if chosen in {"qir", "qir-bitcode"}:
                try:
                    import pyqir
                except ImportError as exc:
                    raise QStackError("Install the qir extra to produce validated QIR bitcode", "MISSING_DEPENDENCY") from exc
                try:
                    module = pyqir.Module.from_ir(pyqir.Context(), payload)
                    problem = module.verify()
                    if problem:
                        raise QStackError("QIR verification failed: " + problem, "INVALID_QIR")
                    payload = module.bitcode
                except (ValueError, RuntimeError) as exc:
                    raise QStackError("QIR could not be assembled/verified", "INVALID_QIR") from exc
                chosen = "qir-bitcode"
        else:
            if chosen not in {"qasm3", "qasm2", "quil", "json"}:
                raise QStackError(f"Unsupported output format: {chosen}", "UNSUPPORTED_FORMAT")
            payload = emit(chosen)
        if compiler_identity() != compiler_tools:
            raise QStackError("Compiler executable changed during compilation; rebuild and retry compilation", "COMPILER_CHANGED")
        if source.read_bytes() != source_bytes:
            raise QStackError("Source changed during compilation; retry with the saved source", "SOURCE_CHANGED")
        manifest = {"compiler_version": __version__, "compiler_tools": compiler_tools, "source_hash": digest(source_text),
                    "source_bytes_sha256": hashlib.sha256(source_bytes).hexdigest(),
                    "optimization_level": optimization_level, "seed": config.get("seed", 42),
                    "statistics": optimization_report(physical, record), "native_spinor": native,
                    "compiler_target": compiler_id, "compilation_owner": "quantum-stack",
                    "qir_entry_point": "main" if chosen.startswith("qir") else None,
                    "readout_order": "c[n-1]...c[0]", "approximation_error_budget": 0,
                    "limits": {"expanded_operation_budget": config.get("expanded_operation_budget", 100000),
                               "placement": {"beam_width": config.get("placement_beam_width", (16,16,32,64)[optimization_level]),
                                   "max_states": config.get("placement_max_states", (10000,10000,50000,200000)[optimization_level]),
                                   "max_layouts": config.get("placement_max_layouts", 8 if optimization_level == 3 else 1)}}}
        artifact = CompiledArtifact(route, target, chosen, payload, physical, record, manifest,
                                    schema_version=ARTIFACT_VERSION, logical_ir=logical,
                                    numerical_report=numerical, feature_requirements=requirements)
        logical_stats = optimization_report(logical, {})
        manifest["comparison"] = {"before": {k: logical_stats.get(k) for k in ("gate_count", "two_qubit_count", "depth")},
                                  "after": {k: manifest["statistics"].get(k) for k in ("gate_count", "two_qubit_count", "depth")},
                                  "compilation_time_seconds": time.perf_counter() - started,
                                  "trial_statistics": numerical.get("trial_statistics", {})}
        artifact.manifest["artifact_hash"] = artifact_hash(artifact)
        if output is not None:
            save_artifact(artifact, output)
        return artifact


def _execute_local(artifact: CompiledArtifact, options: SubmissionOptions, config: dict) -> tuple[JobReceipt, ExecutionResult]:
    native = artifact.manifest.get("native_spinor")
    if not native:
        raise QStackError("Artifact lacks native Spinor IR required by the local simulator", "ARTIFACT_INVALID")
    with tempfile.TemporaryDirectory(prefix="qstack-local-") as temp:
        scratch = Path(temp)
        root, compiler_id = _compiler_registry(artifact.target_snapshot, config, scratch)
        path = scratch / "native.spinor"
        path.write_text(native, encoding="utf-8")
        env = dict(os.environ, SPINOR_REGISTRY_ROOT=str(root))
        data = json.loads(_process([find_binary("spinorc", config), "simulate", "--compiled", "-t", compiler_id,
            "--shots", str(options.shots), "--seed", str(config.get("seed", 42)), str(path)], env))
    counts = data.get("counts", data)
    if not isinstance(counts, dict) or any(type(v) is not int or v < 0 for v in counts.values()) or sum(counts.values()) != options.shots:
        raise QStackError("Local simulator returned an invalid histogram", "SIMULATION_FAILED")
    job_id = "local-" + uuid.uuid4().hex
    metadata = {"mode": "local", "bit_order": "c[n-1]...c[0]", "artifact_hash": artifact_hash(artifact),
                "num_clbits": artifact.physical_ir["num_clbits"],
                "measurement_mapping": artifact.physical_ir.get("measurement_mapping", []),
                "logical_to_physical": artifact.physical_ir.get("logical_to_physical", [])}
    metadata["application_outputs"] = application_outputs(artifact)
    receipt = JobReceipt(artifact.route, artifact.target, job_id, metadata=dict(metadata),
                         artifact_hash=metadata["artifact_hash"], mode="local")
    return receipt, ExecutionResult(artifact.route, artifact.target, job_id, counts, data,
                                   metadata)


def submit_artifact(artifact: CompiledArtifact, options: SubmissionOptions, config: dict | None = None,
                    *, wait=False, dry_run=False):
    config = dict(config or {})
    if artifact.manifest.get("artifact_hash") and artifact.manifest["artifact_hash"] != artifact_hash(artifact):
        raise QStackError("Sealed artifact was modified; compile and save a new artifact", "ARTIFACT_INVALID")
    if config.get("qubit_labels") is not None and config["qubit_labels"] != artifact.target_snapshot.get("qubit_labels"):
        raise QStackError("Physical qubit labels conflict with the compiled artifact; recompile", "TARGET_INCOMPATIBLE")
    if config.get("provider", artifact.route) != artifact.route:
        raise QStackError("Submission route differs from the compiled artifact; recompile for that route")
    validate_physical(artifact.physical_ir, artifact.target_snapshot, config)
    from .classical import extract_requirements, validate_classical, validate_requirements, serializer_capabilities, needs_controller_contract
    validate_classical(artifact.physical_ir)
    requirements = extract_requirements(artifact.physical_ir)
    if artifact.feature_requirements and artifact.feature_requirements != requirements:
        raise QStackError("Artifact feature requirements disagree with physical IR; recompile", "ARTIFACT_INVALID")
    if needs_controller_contract(requirements):
        validate_requirements(requirements, artifact.target_snapshot, serializer_capabilities(artifact.format))
    if dry_run:
        if options.mode == "live":
            from .providers import validate_serialization
            validate_format(artifact.route, artifact.format, artifact.target_snapshot)
            serialization = validate_serialization(artifact, {**config, "shots": options.shots})
        else:
            serialization = {"serializer": "owned-physical-ir", "validated": True, "network_used": False}
        return {"mode": options.mode, "dry_run": True, "route": artifact.route, "target": artifact.target,
                "format": artifact.format, "shots": options.shots, "artifact_hash": artifact_hash(artifact),
                "serialization": serialization}
    if options.mode == "local":
        receipt, result = _execute_local(artifact, options, config)
        save_job(receipt, result, config)
        return result if wait else receipt
    if options.mode == "cassette":
        from importlib.resources import files
        path = files("spinor_submit").joinpath("cassettes", artifact.route, options.name + ".json")
        if not path.is_file():
            raise QStackError("No matching cassette fixture", "CASSETTE_MISSING")
        raw = json.loads(path.read_text())
        metadata = {"mode": "cassette", "fixture": options.name, "artifact_hash": artifact_hash(artifact),
                    "num_clbits": artifact.physical_ir["num_clbits"],
                    "measurement_mapping": artifact.physical_ir.get("measurement_mapping", []),
                    "application_outputs": application_outputs(artifact)}
        receipt = JobReceipt(artifact.route, artifact.target, "cassette-" + uuid.uuid4().hex,
                             metadata=dict(metadata), artifact_hash=metadata["artifact_hash"], mode="cassette")
        result = ExecutionResult(artifact.route, artifact.target, receipt.job_id, None, raw, metadata)
        save_job(receipt, result, config)
        return result if wait else receipt
    ensure_live_target(artifact.target_snapshot)
    config.setdefault("device", artifact.target_snapshot["device"])
    from .providers import get_adapter
    adapter = get_adapter(artifact.route, config)
    records = read_with_retry(adapter.discover)
    current = next((r for r in records if r.get("device") == config["device"]), None)
    if current is None or not current.get("capability_verified"):
        raise QStackError("Cannot revalidate current device capabilities; refresh targets before submitting", "TARGET_REFRESH_REQUIRED")
    validate_physical(artifact.physical_ir, current, config)
    validate_format(artifact.route, artifact.format, current)
    if needs_controller_contract(requirements):
        validate_requirements(requirements, current, serializer_capabilities(artifact.format))
    if current.get("parameter_units", "radians") != artifact.target_snapshot.get("parameter_units", "radians"):
        raise QStackError("Target parameter units changed; refresh and recompile", "TARGET_INCOMPATIBLE")
    if artifact.format.startswith("qir") and current.get("qir_platform", "standard") != artifact.target_snapshot.get("qir_platform", "standard"):
        raise QStackError("Target QIR runtime contract changed; refresh and recompile", "TARGET_INCOMPATIBLE")
    if config["device"] != artifact.target_snapshot["device"]:
        raise QStackError("Submission device differs from the compiled target; recompile", "TARGET_INCOMPATIBLE")
    old_labels = artifact.target_snapshot.get("qubit_labels")
    if old_labels is not None and current.get("qubit_labels") != old_labels:
        raise QStackError("Device qubit labels changed; refresh and recompile", "TARGET_INCOMPATIBLE")
    from .target_models import target_fingerprints
    old_fingerprints, current_fingerprints = target_fingerprints(artifact.target_snapshot), target_fingerprints(current)
    performance_stale = old_fingerprints["quality_hash"] != current_fingerprints["quality_hash"]
    if options.cost_cap_usd is not None:
        estimate = read_with_retry(lambda: adapter.estimate(artifact, options)) if hasattr(adapter, "estimate") else None
        amount = estimate.get("usd") if isinstance(estimate, dict) else None
        if isinstance(amount, bool) or not isinstance(amount, (int, float)) or not math.isfinite(amount) or amount < 0:
            raise QStackError("This route cannot evaluate the requested USD cost cap", "COST_UNKNOWN")
        if amount > options.cost_cap_usd:
            raise QStackError("Estimated execution cost exceeds --cost-cap-usd", "COST_CAP_EXCEEDED")
    receipt = adapter.submit(artifact, options)
    receipt.artifact_hash = artifact_hash(artifact)
    receipt.metadata["application_outputs"] = application_outputs(artifact)
    receipt.metadata["performance_estimates_stale"] = performance_stale
    receipt.metadata["submission_target_fingerprints"] = current_fingerprints
    # Device classification is separate from adapter transport markers such as
    # Qibolab's execution_kind='synchronous-lab', needed for later retrieval.
    receipt.metadata["device_execution_kind"] = current.get("execution_kind", "unknown")
    receipt.metadata["device_execution_kind_verified"] = current.get("execution_kind_verified") is True
    receipt.metadata["device_execution_kind_source"] = current.get("execution_kind_source")
    receipt.metadata["context"] = {k: v for k, v in config.items() if k not in SECRET_FIELDS and k in {
        "device", "project", "workspace", "workspace_resource_id", "resource", "region", "instance_crn", "url", "host", "user", "realm",
        "sdk_profile", "sdk_config_file", "tokens_file", "credentials_file", "qibolab_bridge", "qibolab_platform", "platform", "platform_path", "capability_snapshot", "device_config_name",
        "run_name", "snapshot_id", "calibration_set_id", "quantum_computer", "s3_uri"}}
    receipt.metadata["provider_processing"] = {k: v for k, v in adapter.capabilities.items() if "translation" in k or "transpilation" in k}
    save_job(receipt, config=config)
    if not wait:
        return receipt
    if not adapter.capabilities.get("status", False):
        result = read_with_retry(lambda: adapter.results(receipt))
        save_job(receipt, result, config)
        return result
    return wait_for_result(adapter, receipt, timeout=config.get("timeout", 600), poll_interval=config.get("poll_interval", 2), config=config)


def application_outputs(artifact):
    values = {v["id"]: v for v in artifact.physical_ir.get("classical_values", [])}
    return [{**output, "bits": values[output["value"]]["storage"]}
            for output in artifact.physical_ir.get("classical_outputs", [])]


def run_source(source: str, *, language: str = "phonon", target: str, mode: str,
               shots: int = 1024, **options) -> ExecutionResult:
    config = dict(options.pop("config", {}) or {})
    config.update(options)
    with tempfile.TemporaryDirectory(prefix="qstack-source-") as temp:
        suffix = {"photon": ".pho", "phonon": ".phonon", "spinor": ".spn", "qasm": ".qasm"}.get(language)
        if suffix is None:
            raise QStackError("Unsupported source language")
        path = Path(temp) / ("program" + suffix)
        path.write_text(source, encoding="utf-8")
        artifact = compile_file(path, target=target, language=language, config=config,
                                optimization_level=config.get("optimization_level", 2))
        return submit_artifact(artifact, SubmissionOptions(shots=shots, mode=mode), config, wait=True)
