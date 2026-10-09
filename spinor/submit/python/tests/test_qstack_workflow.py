import copy
import json
from pathlib import Path

import pytest

from qstack.artifacts import load_artifact, save_artifact
from qstack.jobs import load_job, save_job
from qstack.models import CompiledArtifact, ExecutionResult, JobReceipt, QStackError, SubmissionOptions
from qstack.registry import validate_physical, profiles
from qstack.service import compile_file, find_binary, submit_artifact


def bell_artifact():
    ir = {"schema_version": 1, "num_qubits": 2, "num_clbits": 3, "global_phase": 0,
          "instructions": [{"op": "h", "qubits": [0], "params": [], "clbits": []},
             {"op": "cx", "qubits": [0, 1], "params": [], "clbits": []},
             {"op": "measure", "qubits": [0], "params": [], "clbits": [2]},
             {"op": "measure", "qubits": [1], "params": [], "clbits": [0]}],
          "measurement_mapping": [{"qubit": 0, "clbit": 2}, {"qubit": 1, "clbit": 0}]}
    target = {"id": "fixture", "device": "fixture", "route": "ibm", "qubits": 2, "native_gates": ["h", "cx"],
              "coupling": [[0, 1]], "directed_connectivity": True, "capability_verified": True,
              "formats": ["qasm3"],
              "supports": {"mid_circuit_measure": False, "feedforward": "none", "reset": False}}
    return CompiledArtifact("ibm", "fixture", "qasm3", "OPENQASM 3.0;", ir, target)


def test_artifact_roundtrip_and_tampering(tmp_path):
    artifact = bell_artifact()
    save_artifact(artifact, tmp_path / "bundle")
    loaded = load_artifact(tmp_path / "bundle")
    assert loaded.physical_ir == artifact.physical_ir
    (tmp_path / "bundle" / "physical.json").write_text("{}")
    with pytest.raises(QStackError, match="hash mismatch"):
        load_artifact(tmp_path / "bundle")


def test_readout_directions_native_legality():
    artifact = bell_artifact()
    validate_physical(artifact.physical_ir, artifact.target_snapshot)
    bad = copy.deepcopy(artifact.physical_ir)
    bad["instructions"][1]["qubits"] = [1, 0]
    with pytest.raises(QStackError, match="disconnected"):
        validate_physical(bad, artifact.target_snapshot)
    bad = copy.deepcopy(artifact.physical_ir)
    bad["instructions"][-1]["clbits"] = [3]
    with pytest.raises(QStackError, match="classical bit"):
        validate_physical(bad, artifact.target_snapshot)


def test_gate_specific_loci_are_enforced():
    artifact = bell_artifact()
    artifact.target_snapshot["gate_loci"] = {"h": [[1]], "cx": [[0, 1]]}
    with pytest.raises(QStackError, match="unavailable"):
        validate_physical(artifact.physical_ir, artifact.target_snapshot)


def test_jobs_survive_restart_and_strip_secrets(tmp_path, monkeypatch):
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path))
    receipt = JobReceipt("ibm", "device", "provider-job-id", {"api_key": "secret", "context": {"project": "p"}})
    result = ExecutionResult("ibm", "device", receipt.job_id, {"101": 16})
    reference = save_job(receipt, result)
    assert "secret" not in Path(reference).read_text()
    recovered, counts = load_job(receipt.job_id)
    assert recovered.metadata["context"]["project"] == "p"
    assert counts.counts == {"101": 16}


def test_dry_run_is_offline_even_with_live_mode(monkeypatch):
    import qstack.providers
    monkeypatch.setattr(qstack.providers, "get_adapter", lambda *args: pytest.fail("dry-run must not construct a live client"))
    summary = submit_artifact(bell_artifact(), SubmissionOptions(mode="live"), dry_run=True)
    assert summary["dry_run"]


def test_cost_cap_unknown_blocks_submission(monkeypatch):
    artifact = bell_artifact()
    class Adapter:
        def discover(self): return [artifact.target_snapshot]
        def submit(self, *args): pytest.fail("cost cap must stop job creation")
    monkeypatch.setattr("qstack.providers.get_adapter", lambda *args: Adapter())
    with pytest.raises(QStackError) as exc:
        submit_artifact(artifact, SubmissionOptions(mode="live", cost_cap_usd=1))
    assert exc.value.code == "COST_UNKNOWN"


def test_no_retry_after_ambiguous_submit(monkeypatch):
    artifact = bell_artifact()
    calls = []
    class Adapter:
        def discover(self): return [artifact.target_snapshot]
        def submit(self, *args):
            calls.append(1)
            raise QStackError("ambiguous timeout", "PROVIDER_CONNECTION_ERROR")
    monkeypatch.setattr("qstack.providers.get_adapter", lambda *args: Adapter())
    with pytest.raises(QStackError):
        submit_artifact(artifact, SubmissionOptions(mode="live"))
    assert len(calls) == 1


def test_transient_reads_retry_but_auth_errors_do_not(monkeypatch):
    from qstack.jobs import read_with_retry
    monkeypatch.setattr("qstack.jobs.time.sleep", lambda seconds: None)
    attempts = []
    def read():
        attempts.append(1)
        if len(attempts) < 3:
            raise QStackError("transport timeout", "PROVIDER_CONNECTION_ERROR")
        return "done"
    assert read_with_retry(read) == "done" and len(attempts) == 3
    attempts.clear()
    def expired():
        attempts.append(1)
        raise QStackError("HTTP 401", "PROVIDER_HTTP_ERROR")
    with pytest.raises(QStackError):
        read_with_retry(expired)
    assert len(attempts) == 1


def test_schedule_uses_calibration_resources_and_never_guesses_missing_times():
    from qstack.scheduling import optimization_report
    ir = {"instructions": [{"op": "x", "qubits": [0]}, {"op": "x", "qubits": [1]},
                            {"op": "cz", "qubits": [0, 1]}]}
    target = {"calibration": {"instruction_durations": [
        {"op": "x", "qubits": [0], "duration_ns": 20},
        {"op": "x", "qubits": [1], "duration_ns": 30},
        {"op": "cz", "qubits": [0, 1], "duration_ns": 40}]}}
    report = optimization_report(ir, target)
    assert report["depth"] == 2
    assert report["duration_seconds"] == pytest.approx(70e-9)
    target["scheduling"] = {"exclusive_qubit_groups": [[0, 1]]}
    report = optimization_report(ir, target)
    assert report["depth"] == 3 and report["duration_seconds"] == pytest.approx(90e-9)
    target["calibration"]["instruction_durations"].pop()
    assert optimization_report(ir, target)["duration_seconds"] is None


def test_artifact_native_source_and_optimization_report_are_hashed(tmp_path):
    artifact = bell_artifact()
    artifact.manifest["native_spinor"] = "original"
    save_artifact(artifact, tmp_path / "bundle")
    manifest_file = tmp_path / "bundle" / "manifest.json"
    manifest = json.loads(manifest_file.read_text())
    manifest["native_spinor"] = "changed"
    manifest_file.write_text(json.dumps(manifest))
    with pytest.raises(QStackError, match="manifest hash"):
        load_artifact(tmp_path / "bundle")


def test_sealed_artifact_and_label_overrides_rejected_before_auth(tmp_path, monkeypatch):
    monkeypatch.setattr("qstack.providers.get_adapter", lambda *a: pytest.fail("must fail before authentication"))
    artifact = bell_artifact()
    with pytest.raises(QStackError, match="labels conflict"):
        submit_artifact(artifact, SubmissionOptions(mode="live"), {"qubit_labels": ["different"]})
    save_artifact(artifact, tmp_path / "bundle")
    artifact.physical_ir["global_phase"] = 0.25
    with pytest.raises(QStackError, match="Sealed artifact"):
        submit_artifact(artifact, SubmissionOptions(mode="live"))


def test_stale_readout_map_and_current_format_change_rejected(monkeypatch):
    artifact = bell_artifact()
    bad = copy.deepcopy(artifact.physical_ir)
    bad["measurement_mapping"][0]["clbit"] = 0
    with pytest.raises(QStackError, match="Measurement mapping"):
        validate_physical(bad, artifact.target_snapshot)
    current = {**artifact.target_snapshot, "formats": ["qir-bitcode"]}
    class Adapter:
        def discover(self): return [current]
        def submit(self, *a): pytest.fail("format change must stop job creation")
    monkeypatch.setattr("qstack.providers.get_adapter", lambda *a: Adapter())
    with pytest.raises(QStackError, match="program format"):
        submit_artifact(artifact, SubmissionOptions(mode="live"))


def test_ambiguous_job_id_requires_receipt_path(tmp_path, monkeypatch):
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path))
    first = save_job(JobReceipt("ibm", "device", "same-id"))
    save_job(JobReceipt("aws", "device", "same-id"))
    with pytest.raises(QStackError, match="ambiguous"):
        load_job("same-id")
    assert load_job(first)[0].route == "ibm"


def test_qir_runtime_contract_change_requires_recompilation(monkeypatch):
    artifact = bell_artifact()
    artifact.route, artifact.format = "quantinuum", "qir-bitcode"
    artifact.target_snapshot.update(route="quantinuum", formats=["qir-bitcode"], qir_platform="quantinuum-h2")
    current = {**artifact.target_snapshot, "qir_platform": "quantinuum-helios"}
    class Adapter:
        def discover(self): return [current]
        def submit(self, *args): pytest.fail("runtime change must stop job creation")
    monkeypatch.setattr("qstack.providers.get_adapter", lambda *args: Adapter())
    with pytest.raises(QStackError, match="QIR runtime contract changed"):
        submit_artifact(artifact, SubmissionOptions(mode="live"))


def test_persistence_failure_retains_existing_provider_job_id(monkeypatch):
    def fail(*a): raise OSError("disk full")
    monkeypatch.setattr("qstack.jobs._write_job", fail)
    receipt = JobReceipt("ibm", "device", "created-provider-job", {"api_key": "secret"})
    with pytest.raises(QStackError) as caught:
        save_job(receipt)
    assert caught.value.code == "JOB_PERSISTENCE_FAILED"
    assert caught.value.details["job_receipt"]["job_id"] == receipt.job_id
    assert "secret" not in json.dumps(caught.value.details)


def test_registry_keeps_all_profiles_with_honest_readiness():
    records = profiles()
    assert len(records) == 31
    assert len({r["vendor"] for r in records}) == 11
    assert all(r["readiness"] and not r["capability_verified"] for r in records)
    boson = next(r for r in records if r["id"] == "alicebob_boson_4")
    assert boson["qubits"] == 1 and "cx" not in boson["native_gates"]


def test_photon_to_artifact_to_real_simulator(tmp_path, monkeypatch):
    try:
        find_binary("photonc")
    except QStackError:
        pytest.skip("C++ compiler binaries are not installed")
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path / "state"))
    source = tmp_path / "bell.pho"
    source.write_text("target generic\nkernel bell() -> int {\n QReg q(2)\n q.bell_pair(0, 1)\n return q.measure_int()\n}\n")
    artifact = compile_file(source, target="ibm_fez", output=tmp_path / "artifact")
    assert "transpile" not in artifact.program_text()
    assert set(i["op"] for i in artifact.physical_ir["instructions"]) <= {"rz", "sx", "x", "cz", "measure", "barrier", "gphase"}
    result = submit_artifact(load_artifact(tmp_path / "artifact"), SubmissionOptions(shots=256, mode="local"), wait=True)
    assert set(result.counts) == {"00", "11"}
    assert sum(result.counts.values()) == 256


def test_python_api_requires_explicit_execution_mode():
    with pytest.raises(QStackError) as exc:
        SubmissionOptions()
    assert exc.value.code == "MODE_REQUIRED"


def test_unknown_typed_contract_versions_are_rejected():
    with pytest.raises(QStackError, match="artifact version"):
        CompiledArtifact("ibm", "device", "qasm3", "", {}, schema_version=2)
    with pytest.raises(QStackError, match="options version"):
        SubmissionOptions(mode="live", schema_version=2)
