import copy
import json
import pytest

from qstack.models import QStackError
from qstack.registry import cache_targets
from qstack.scheduling import optimization_report
from qstack.target_models import normalize_target_model, target_fingerprints, attach_timing_model


def target():
    return {"route": "ibm", "device": "fixture", "qubits": 2, "native_gates": ["x", "rz", "cz"],
            "coupling": [[0, 1]], "capability_verified": True,
            "calibration": {"instruction_durations": [
                {"op": "x", "qubits": [q], "duration_ns": 20} for q in [0, 1]]}}


def test_quality_refresh_does_not_change_capability_fingerprint():
    old = target(); new = copy.deepcopy(old)
    new["calibration"]["instruction_durations"][0]["duration_ns"] = 25
    assert target_fingerprints(old)["capability_hash"] == target_fingerprints(new)["capability_hash"]
    assert target_fingerprints(old)["quality_hash"] != target_fingerprints(new)["quality_hash"]
    new["gate_loci"] = {"x": [[1]]}
    assert target_fingerprints(old)["capability_hash"] != target_fingerprints(new)["capability_hash"]
    new = copy.deepcopy(old); new["capabilities"] = {"features": {"classical.add": "supported"}}
    assert target_fingerprints(old)["capability_hash"] != target_fingerprints(new)["capability_hash"]


@pytest.mark.parametrize("invalid", [True, -1, float("nan"), float("inf"), "20", None])
def test_invalid_supplied_duration_rejects_instead_of_guessing(invalid):
    record = target(); record["calibration"]["instruction_durations"][0]["duration_ns"] = invalid
    with pytest.raises(QStackError, match="finite nonnegative"):
        normalize_target_model(record)


@pytest.mark.parametrize("invalid", [True, -1, float("nan"), "0", None])
def test_invalid_feedback_rejects(invalid):
    record = target(); record["scheduling"] = {"feedback_latency_ns": invalid}
    with pytest.raises(QStackError, match="feedback_latency_ns"):
        normalize_target_model(record)


def test_named_resources_serialize_disjoint_qubits_and_preserve_unknown_times():
    record = target(); ir = {"num_qubits": 2, "instructions": [{"op": "x", "qubits": [q]} for q in [0, 1]]}
    record["scheduling"] = {"resources": [{"id": "shared-drive", "capacity": 1}],
        "instruction_resources": [{"op": "x", "qubits": [q], "resources": ["shared-drive"]} for q in [0, 1]]}
    report = optimization_report(ir, record)
    assert [entry["start_ns"] for entry in report["schedule"]] == [0, 20]
    assert report["duration_seconds"] == pytest.approx(40e-9)
    record["calibration"]["instruction_durations"].pop()
    unknown = optimization_report(ir, record)
    assert unknown["duration_seconds"] is None and all(entry["start_ns"] is None for entry in unknown["schedule"])
    record["scheduling"]["instruction_resources"][0]["resources"] = ["invented"]
    with pytest.raises(QStackError, match="unknown"):
        normalize_target_model(record)


def test_parameter_specific_duration_does_not_claim_unmeasured_angle():
    record = target(); record["calibration"]["instruction_durations"] = [
        {"op": "rz", "qubits": [0], "parameters": [0.5], "duration_ns": 10}]
    ir = {"num_qubits": 2, "instructions": [{"op": "rz", "qubits": [0], "params": [0.6]}]}
    assert optimization_report(ir, record)["duration_seconds"] is None
    ir["instructions"][0]["params"] = [0.5]
    assert optimization_report(ir, record)["duration_seconds"] == pytest.approx(10e-9)


@pytest.mark.parametrize("parameters", [None, "0.5", [[0.5]], [True], [float("nan")]])
def test_malformed_parameter_applicability_has_actionable_error(parameters):
    record = target(); record["calibration"]["instruction_durations"][0]["parameters"] = parameters
    with pytest.raises(QStackError, match="parameter applicability"):
        normalize_target_model(record)


def test_invalid_provenance_interval_rejects():
    record = target(); record["calibration"]["provenance"] = {
        "kind": "operator-file", "source": "characterization",
        "observed_at": "2026-10-09T00:00:00Z", "valid_until": "2026-10-08T00:00:00Z"}
    with pytest.raises(QStackError, match="precedes"):
        normalize_target_model(record)


def test_gate_symmetry_does_not_invent_reversed_locus_calibration():
    record = target(); record["calibration"]["instruction_durations"] = [
        {"op": "cz", "qubits": [0, 1], "duration_ns": 100}]
    ir = {"num_qubits": 2, "instructions": [{"op": "cz", "qubits": [1, 0]}]}
    assert optimization_report(ir, record)["duration_seconds"] is None


def test_reused_controller_storage_serializes_independent_ssa_writes():
    record = target(); record["calibration"]["instruction_durations"] = [
        {"op": "c_const", "qubits": [], "duration_ns": 5}]
    ir = {"num_qubits": 2, "classical_values": [{"id": value, "storage": [0]} for value in ["v0", "v1"]],
        "instructions": [{"op": "c_const", "result": value, "value": "0"} for value in ["v0", "v1"]]}
    report = optimization_report(ir, record)
    assert [entry["start_ns"] for entry in report["schedule"]] == [0, 5]
    assert report["duration_seconds"] == pytest.approx(10e-9)


def test_expired_or_future_calibration_suppresses_estimates_at_recorded_time():
    record = target(); record["calibration"]["provenance"] = {
        "kind": "operator-file", "source": "characterization",
        "observed_at": "2026-10-09T00:00:00Z", "valid_until": "2026-10-10T00:00:00Z"}
    ir = {"num_qubits": 2, "instructions": [{"op": "x", "qubits": [0]}]}
    current = optimization_report(ir, record, as_of="2026-10-09T12:00:00Z")
    assert current["timing_complete"] and current["duration_seconds"] == pytest.approx(20e-9)
    assert current["model_validity_at_compile"]["status"] == "within_declared_interval"
    for moment, status in [("2026-10-11T00:00:00Z", "expired"), ("2026-10-08T00:00:00Z", "not_yet_observed")]:
        report = optimization_report(ir, record, as_of=moment)
        assert report["duration_seconds"] is None and not report["timing_complete"]
        assert all(entry["start_ns"] is None for entry in report["schedule"])
        assert report["model_validity_at_compile"]["status"] == status
        assert report["model_validity_at_compile"]["as_of"].startswith(moment[:19])


def test_missing_validity_stays_unknown_and_invalid_as_of_rejects():
    ir = {"num_qubits": 2, "instructions": []}
    report = optimization_report(ir, target(), as_of="2026-10-09T00:00:00Z")
    assert report["model_validity_at_compile"]["status"] == "unknown"
    with pytest.raises(QStackError, match="as_of"):
        optimization_report(ir, target(), as_of="2026-10-09T00:00:00")


def test_ssa_controller_operation_waits_for_measurement_and_is_not_a_quantum_gate():
    record = target(); record["calibration"]["instruction_durations"] = [
        {"op": "measure", "qubits": [0], "duration_ns": 100},
        {"op": "c_copy", "qubits": [], "duration_ns": 5},
        {"op": "x", "qubits": [1], "duration_ns": 20}]
    record["scheduling"] = {"feedback_latency_ns": 25}
    ir = {"num_qubits": 2, "instructions": [
        {"op": "measure", "qubits": [0], "clbits": [0], "result": "v0"},
        {"op": "c_copy", "inputs": ["v0"], "result": "v1"},
        {"op": "if", "condition": "v1"}, {"op": "x", "qubits": [1]}, {"op": "endif"}]}
    report = optimization_report(ir, record)
    assert [entry["start_ns"] for entry in report["schedule"]] == [0, 100, 130]
    assert report["gate_count"] == 1 and report["duration_seconds"] == pytest.approx(150e-9)


def test_bound_operator_model_and_source_provenance(tmp_path):
    record = target()
    model = {"schema_version": 1, "route": "ibm", "device": "fixture",
        "capability_hash": target_fingerprints(record)["capability_hash"],
        "provenance": {"kind": "operator-file", "source": "lab characterization", "observed_at": "2026-10-09T00:00:00Z"},
        "scheduling": {"feedback_latency_ns": 12}}
    path = tmp_path / "timing model.json"; path.write_text(json.dumps(model))
    bound = attach_timing_model(record, path)
    assert bound["scheduling"]["feedback_latency_ns"] == 12
    assert bound["timing_model_provenance"] == model["provenance"]
    assert record == target()  # original immutable record is untouched
    record["device"] = "another"
    with pytest.raises(QStackError, match="exact route and device"):
        attach_timing_model(record, path)


def test_new_snapshots_keep_separate_fingerprints_without_changing_old_files(tmp_path, monkeypatch):
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path))
    saved = cache_targets("ibm", [target()])[0]
    assert saved["schema_version"] == 2
    assert saved["capability_hash"] and saved["quality_hash"]
    assert "retrieved_at" in saved and "snapshot_hash" in saved
