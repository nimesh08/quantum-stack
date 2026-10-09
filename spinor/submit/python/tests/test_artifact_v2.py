import copy
import hashlib
import json

import pytest

from qstack.artifacts import artifact_hash, load_artifact, numerical_evidence, save_artifact
from qstack.classical import exact_integer, feature_support, validate_classical, validate_requirements
from qstack.models import CompiledArtifact, ExecutionResult, JobReceipt, QStackError
from qstack.registry import canonical_json


def artifact(version=2):
    ir = {"schema_version": version, "num_qubits": 1, "num_clbits": 1, "instructions": [], "global_phase": 0}
    return CompiledArtifact("ibm", "offline", "qasm3", "OPENQASM 3.0;", ir,
                            schema_version=version, logical_ir=copy.deepcopy(ir) if version == 2 else None)


def test_v1_original_hash_preimage_and_missing_evidence(tmp_path):
    old = artifact(1)
    preimage = {"schema_version": 1, "route": "ibm", "target": "offline", "format": "qasm3",
                "payload_hash": hashlib.sha256(b"OPENQASM 3.0;").hexdigest(),
                "physical_ir": old.physical_ir, "target_snapshot": {}, "compilation": {}}
    expected = hashlib.sha256(canonical_json(preimage)).hexdigest()
    assert artifact_hash(old) == expected
    save_artifact(old, tmp_path / "old")
    restored = load_artifact(tmp_path / "old")
    assert restored.schema_version == 1 and artifact_hash(restored) == expected
    assert numerical_evidence(restored)["whole_program_error"] is None
    assert numerical_evidence(restored)["status"] == "unavailable"
    assert not (tmp_path / "old" / "logical.json").exists()


def test_v2_evidence_sealed_and_roundtrip(tmp_path):
    new = artifact()
    new.numerical_report = {"schema_version": 1, "certified": False, "whole_program_error": None, "coverage": {"complete": False}}
    save_artifact(new, tmp_path / "new")
    restored = load_artifact(tmp_path / "new")
    assert restored.logical_ir == new.logical_ir
    assert artifact_hash(restored) == artifact_hash(new)
    assert {"logical.json", "numerical.json", "classical.json", "requirements.json"} <= set(restored.manifest["files"])
    restored.logical_ir["instructions"].append({"op": "x", "qubits": [0]})
    assert artifact_hash(restored) != new.manifest["artifact_hash"]
    (tmp_path / "new" / "numerical.json").write_text("{}")
    with pytest.raises(QStackError, match="hash mismatch"):
        load_artifact(tmp_path / "new")


def test_v2_never_silently_upgrades_v1_ir():
    with pytest.raises(QStackError, match="physical IR v2"):
        CompiledArtifact("ibm", "d", "qasm3", "", {"schema_version": 1}, schema_version=2, logical_ir={})
    assert JobReceipt.from_dict({"route": "ibm", "target": "d", "job_id": "old"}).schema_version == 1
    assert ExecutionResult.from_dict({"route": "ibm", "target": "d", "job_id": "old", "counts": {"000": 2}}).counts == {"000": 2}


def test_exact_wide_integer_contract():
    assert exact_integer("18446744073709551615") == 2**64-1
    assert exact_integer("9007199254740993") == 2**53+1
    for malformed in (2**53+1, float(2**53+1), "01", "-1", "+2", "1.0", "18446744073709551616"):
        with pytest.raises(QStackError): exact_integer(malformed)


def test_feedforward_does_not_authorize_arithmetic():
    device = {"supports": {"feedforward": "full"}}
    assert feature_support(device, "branching.bit") == "supported"
    assert feature_support(device, "classical.add") == "unknown"
    allowed = {"capabilities": {"features": {"classical.add": "supported"}, "integer_widths": [8]}}
    requirements = {"features": ["classical.add"], "integer_widths": [8]}
    with pytest.raises(QStackError, match="device.*unknown"):
        validate_requirements(requirements, device, allowed)
    with pytest.raises(QStackError, match="output format.*unknown"):
        validate_requirements(requirements, allowed, device)
    validate_requirements(requirements, allowed, allowed)


def test_loop_status_keeps_correlated_shots_and_raw(tmp_path, monkeypatch):
    from qstack.jobs import save_job, load_job
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path))
    receipt = JobReceipt("ibm", "d", "job", {"application_outputs": [{"role": "loop_exhausted", "bits": [2]}]})
    result = ExecutionResult("ibm", "d", "job", {"000": 17, "101": 3}, raw={"provider": "unchanged"})
    ref = save_job(receipt, result)
    restored = load_job(ref)[1]
    assert restored.metadata["application_status"] == "loop_exhausted"
    assert restored.metadata["exhausted_shots"] == 3
    assert restored.counts == {"000": 17, "101": 3} and restored.raw == result.raw


@pytest.mark.parametrize("counts,exit_status,application_status", [({"000": 7}, 0, "completed"),
    ({"000": 4, "100": 3}, 4, "loop_exhausted"), (None, 5, "not_checked")])
def test_cli_saved_loop_results_have_distinct_exit_status(tmp_path, monkeypatch, capsys, counts, exit_status, application_status):
    from qstack.cli import main
    from qstack.jobs import save_job, load_job
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path))
    receipt = JobReceipt("ibm", "offline", "loop-job", mode="local", metadata={
        "application_outputs": [{"name": "loop_exhausted_0", "role": "loop_exhausted", "bits": [2]}]})
    result = ExecutionResult("ibm", "offline", "loop-job", counts, raw={"samples": "retained"})
    ref = save_job(receipt, result)
    assert main(["jobs", "results", ref, "--no-env-file", "--json"]) == exit_status
    displayed = json.loads(capsys.readouterr().out)
    assert displayed["metadata"]["application_status"] == application_status
    assert load_job(ref)[1].raw == {"samples": "retained"}


def test_v2_evidence_is_sidecar_bound_without_changing_artifact(tmp_path, monkeypatch, capsys):
    from qstack.cli import main
    import socket
    monkeypatch.setattr(socket, "socket", lambda *a, **k: pytest.fail("verify opened a network socket"))
    compiled = artifact()
    compiled.payload = 'OPENQASM 3.0; include "stdgates.inc"; qubit[1] q; bit[1] c;'
    directory = save_artifact(compiled, tmp_path / "compiled artifact")
    identity = artifact_hash(compiled)
    manifest_bytes = (directory / "manifest.json").read_bytes()
    assert main(["verify", str(directory), "--no-env-file", "--json"]) == 0
    report = json.loads(capsys.readouterr().out)
    assert report["artifact_hash"] == identity
    assert report["tools"]["oracle_source_sha256"]
    assert report["network_used"] is False
    assert len(list((directory / "verification").glob("*.json"))) == 1
    assert (directory / "manifest.json").read_bytes() == manifest_bytes
    assert artifact_hash(load_artifact(directory)) == identity

