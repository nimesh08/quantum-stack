"""Device attestation must not replace transport state or guess backend names."""
from types import SimpleNamespace
import pytest
from qstack.jobs import load_job
from qstack.models import CompiledArtifact, SubmissionOptions
from qstack.providers import cloud
from qstack.providers.qibolab import QibolabAdapter
from qstack.service import submit_artifact


@pytest.mark.parametrize("simulator,kind", [(True, "simulator"), (False, "hardware"),
    (None, None), (0, None), ("false", None)])
def test_ibm_device_type_uses_strict_configuration_boolean(monkeypatch, simulator, kind):
    backend = SimpleNamespace(name="name-does-not-establish-hardware",
                              configuration=lambda: SimpleNamespace(simulator=simulator))
    monkeypatch.setattr(cloud, "qiskit_target_record", lambda route, backend, formats:
                        {"route": route, "device": backend.name, "formats": formats})
    record = cloud.IBMAdapter({"_client": SimpleNamespace(backends=lambda: [backend])}).discover()[0]
    assert record.get("execution_kind") == kind
    assert record.get("execution_kind_verified", False) is (kind is not None)


@pytest.mark.parametrize("device_type,kind", [("QPU", "hardware"), ("SIMULATOR", "simulator"),
    ("unknown", None), (None, None)])
def test_braket_device_type_uses_sdk_type_not_qpu_arn(monkeypatch, device_type, kind):
    device = SimpleNamespace(type=device_type, arn="arn:aws:braket:us-east-1::device/qpu/ionq/name",
        provider_name="IonQ", properties={"paradigm": {"nativeGateSet": ["x"], "qubitCount": 1,
                                                     "connectivity": {"fullyConnected": True}}})
    sdk = SimpleNamespace(AwsDevice=SimpleNamespace(get_devices=lambda **kwargs: [device]))
    monkeypatch.setattr(cloud, "optional", lambda name, extra: sdk)
    record = cloud.AWSAdapter({"_client": object()}).discover()[0]
    assert record.get("execution_kind") == kind
    assert record.get("execution_kind_verified", False) is (kind is not None)


def test_service_persists_qibolab_transport_marker_and_retrieves_after_restart(monkeypatch, tmp_path):
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path))
    calls = []
    class Platform:
        def connect(self): calls.append("connect")
        def disconnect(self): calls.append("disconnect")
        def execute(self, sequences, **options):
            calls.append("execute")
            assert sequences == ["fixture-sequence"] and options["nshots"] == 2
            return {"acquisition": [[1], [0]]}
    snapshot = {"route": "qibolab", "vendor": "tii", "device": "configured-fixture", "qubits": 1,
        "native_gates": ["x"], "all_to_all": True, "coupling": [], "formats": ["qibolab-native"],
        "capability_verified": True, "readiness": "ready", "execution_kind": "hardware",
        "execution_kind_verified": True, "execution_kind_source": "explicit laboratory contract",
        "supports": {"mid_circuit_measure": False, "reset": False, "feedforward": False}}
    ir = {"schema_version": 1, "num_qubits": 1, "num_clbits": 1,
          "instructions": [{"op": "measure", "qubits": [0], "clbits": [0], "params": []}],
          "measurement_mapping": [{"qubit": 0, "clbit": 0}]}
    artifact = CompiledArtifact("qibolab", snapshot["device"], "qibolab-native", "{}", ir, snapshot)
    bridge = {"snapshot": snapshot, "platform": Platform(), "build_sequences": lambda artifact: ["fixture-sequence"]}
    monkeypatch.setattr("qstack.providers.qibolab.optional", lambda *args: SimpleNamespace(
        AcquisitionType=SimpleNamespace(DISCRIMINATION="discrimination"),
        AveragingMode=SimpleNamespace(SINGLESHOT="single-shot")))
    receipt = submit_artifact(artifact, SubmissionOptions(mode="live", shots=2), {"_bridge": bridge})
    assert calls == ["connect", "execute", "disconnect"]
    assert receipt.metadata["execution_kind"] == "synchronous-lab"
    assert receipt.metadata["device_execution_kind"] == "hardware"
    assert receipt.metadata["device_execution_kind_verified"] is True
    reloaded, _ = load_job(receipt.job_id)
    result = QibolabAdapter({}).results(reloaded)  # No platform or authentication is needed to retrieve.
    assert result.raw == {"acquisition": [[1], [0]]}
    assert result.counts is None  # A custom bridge supplied no readout mapping; no histogram is invented.
    assert calls == ["connect", "execute", "disconnect"]
