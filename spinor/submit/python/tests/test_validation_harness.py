import json
from types import SimpleNamespace
import pytest
from qstack import validation_harness as harness
from qstack.models import CompiledArtifact, JobReceipt, ExecutionResult, QStackError


@pytest.fixture
def environment(monkeypatch, tmp_path):
    calls = []
    artifact = CompiledArtifact("ibm", "fixture", "qasm3", "OPENQASM 3.0;", {
        "num_qubits": 1, "num_clbits": 1, "instructions": [], "measurement_mapping": []},
        target_snapshot={"route": "ibm", "device": "fixture", "qubits": 1})
    receipt = JobReceipt("ibm", "fixture", "job-1")
    result = ExecutionResult("ibm", "fixture", "job-1", {"0": 128}, {"raw": "provider"})
    def submit(artifact, options, config, **kwargs):
        calls.append("serialize" if kwargs.get("dry_run") else "create")
        return {"dry_run": True} if kwargs.get("dry_run") else receipt
    def authenticate():
        calls.append("auth")
        return {"authenticated": True}
    adapter = SimpleNamespace(capabilities={"status": False}, check_auth=authenticate,
                              results=lambda saved: result)
    def get_adapter(*args):
        calls.append("adapter")
        return adapter
    monkeypatch.setattr(harness, "submit_artifact", submit)
    monkeypatch.setattr(harness, "get_adapter", get_adapter)
    monkeypatch.setattr(harness, "verify_artifact", lambda *a, **kw: {"status": "passed"})
    monkeypatch.setattr(harness, "save_job", lambda *a, **kw: str(tmp_path / "receipt.json"))
    return artifact, calls, adapter, result


def test_offline_is_default_and_never_constructs_authenticated_adapter(environment):
    artifact, calls, _, _ = environment
    evidence = harness.run_validation(artifact)
    assert calls == ["serialize"]
    assert evidence["contract_tested"] and not evidence["account_access_verified"]
    assert evidence["submission_attempts"] == 0


def test_account_validation_is_read_only(environment):
    artifact, calls, _, _ = environment
    evidence = harness.run_validation(artifact, level="account")
    assert calls == ["serialize", "adapter", "auth"]
    assert evidence["account_access_verified"] and not evidence["live_execution_completed"]


@pytest.mark.parametrize("route,allowed", [("qibolab", True), ("ibm", False)])
def test_configured_laboratory_is_separate_from_cloud_account_access(environment, route, allowed):
    artifact, calls, adapter, _ = environment
    artifact.route = route; artifact.target_snapshot["route"] = route
    adapter.check_auth = lambda: {"authenticated": None, "configured": True}
    evidence = harness.run_validation(artifact, level="hardware")
    assert evidence["laboratory_configured"] is allowed
    assert evidence["account_access_verified"] is False
    assert calls.count("create") == (1 if allowed else 0)
    assert evidence["live_execution_completed"] is allowed
    assert evidence["hardware_execution_verified"] is False  # Configuration alone does not establish device type.


def test_failed_oracle_prevents_authentication_and_job_creation(environment, monkeypatch):
    artifact, calls, _, _ = environment
    monkeypatch.setattr(harness, "verify_artifact", lambda *a, **kw: {"status": "failed"})
    evidence = harness.run_validation(artifact, level="hardware")
    assert calls == ["serialize"]
    assert evidence["error"]["code"] == "VERIFICATION_FAILED"


def test_live_result_requires_verified_execution_kind_for_hardware_claim(environment, tmp_path):
    artifact, calls, _, _ = environment
    evidence = harness.run_validation(artifact, level="hardware", output=tmp_path / "evidence.json",
        expected_bitstrings=["0"], minimum_success_probability=.9)
    assert calls.count("create") == 1 and evidence["live_execution_completed"]
    assert not evidence["hardware_execution_verified"]
    assert evidence["sampling"]["status"] == "passed"
    assert json.loads((tmp_path / "evidence.json").read_text())["receipt"]["job_id"] == "job-1"


def test_probabilities_never_become_counts(environment):
    artifact, _, _, result = environment
    result.counts = None; result.raw = {"probabilities": {"0": .8, "1": .2}}
    evidence = harness.run_validation(artifact, level="hardware", expected_bitstrings=["0"])
    assert evidence["sampling"]["status"] == "not_checked"
    assert evidence["result"]["raw"] == result.raw


@pytest.mark.parametrize("fresh_kind,verified,expected", [("hardware", True, True), ("simulator", True, False), ("hardware", False, False)])
def test_hardware_classification_uses_fresh_receipt_not_artifact(environment, monkeypatch, fresh_kind, verified, expected):
    artifact, _, _, _ = environment
    artifact.target_snapshot.update(execution_kind="hardware", execution_kind_verified=True)
    original = harness.submit_artifact
    def submit(*args, **kwargs):
        response = original(*args, **kwargs)
        if not kwargs.get("dry_run"):
            response.metadata.update(device_execution_kind=fresh_kind, device_execution_kind_verified=verified)
        return response
    monkeypatch.setattr(harness, "submit_artifact", submit)
    evidence = harness.run_validation(artifact, level="hardware")
    assert evidence["execution_kind"] == fresh_kind
    assert evidence["hardware_execution_verified"] is expected


def test_ambiguous_creation_is_not_retried(environment, monkeypatch, tmp_path):
    artifact, calls, _, _ = environment
    original = harness.submit_artifact
    def submit(*args, **kwargs):
        if kwargs.get("dry_run"):
            return original(*args, **kwargs)
        calls.append("timeout")
        raise QStackError("Connection failed", "PROVIDER_CONNECTION_ERROR")
    monkeypatch.setattr(harness, "submit_artifact", submit)
    evidence = harness.run_validation(artifact, level="hardware", output=tmp_path / "failed.json")
    assert calls.count("timeout") == 1
    assert evidence["do_not_resubmit_automatically"] and evidence["status"] == "failed"


def test_evidence_redacts_successful_provider_echoes(environment, tmp_path):
    artifact, _, _, result = environment
    result.raw = {"echo": "secret-credential"}
    evidence = harness.run_validation(artifact, level="hardware", config={"api_key": "secret-credential"},
        output=tmp_path / "redacted.json")
    assert "secret-credential" not in json.dumps(evidence)
    assert "secret-credential" not in (tmp_path / "redacted.json").read_text()


@pytest.mark.parametrize("stage", ["authentication", "results"])
def test_sdk_exceptions_persist_failure_without_exposing_session_text(environment, monkeypatch, tmp_path, stage):
    artifact, calls, adapter, _ = environment
    monkeypatch.setattr("qstack.jobs.time.sleep", lambda _: None)
    def failed(*args):
        raise TimeoutError("session token sdk-managed-private-value")
    setattr(adapter, "check_auth" if stage == "authentication" else "results", failed)
    destination = tmp_path / "sdk-failed.json"
    evidence = harness.run_validation(artifact, level="hardware", output=destination)
    assert evidence["status"] == "failed" and evidence["error"]["code"] == "PROVIDER_SDK_ERROR"
    assert evidence["error"]["exception_type"] == "TimeoutError"
    assert calls.count("create") == (1 if stage == "results" else 0)
    assert evidence["do_not_resubmit_automatically"] is (stage == "results")
    if stage == "results": assert evidence["receipt"]["job_id"] == "job-1"
    assert "sdk-managed-private-value" not in destination.read_text()


@pytest.mark.parametrize("value", [True, -1, float("nan"), 1.1])
def test_invalid_sampling_threshold_rejects_before_any_action(environment, value):
    artifact, calls, _, _ = environment
    with pytest.raises(QStackError):
        harness.run_validation(artifact, level="hardware", expected_bitstrings=["0"], minimum_success_probability=value)
    assert calls == []
