"""Actual pinned SDK models/signatures, without accounts, network or compilers.

QSTACK_TEST_SDK selects the CI extra. Its missing dependencies fail rather than
skip; unrelated tests skip. With no selector, available SDKs are checked.
"""
from __future__ import annotations

import importlib
import hashlib
import inspect
import json
import math
import os
from pathlib import Path
import socket
from types import SimpleNamespace as NS
from uuid import UUID

import pytest

from qstack.models import CompiledArtifact, SubmissionOptions
from qstack.providers import get_adapter
from qstack.providers.native import serialize_native


def sdk(route, name):
    selected = os.environ.get("QSTACK_TEST_SDK")
    if selected and selected != route:
        pytest.skip(f"SDK matrix selected {selected}")
    if selected:
        return importlib.import_module(name)
    return pytest.importorskip(name)


@pytest.fixture(autouse=True)
def no_network(monkeypatch):
    def forbidden(*args, **kwargs):
        pytest.fail("SDK contract tests must not open a network connection")
    monkeypatch.setattr(socket.socket, "connect", forbidden)
    monkeypatch.setattr(socket.socket, "connect_ex", forbidden)


def artifact(route, fmt, payload, operations=None, snapshot=None):
    ir = {"schema_version": 1, "num_qubits": 2, "num_clbits": 2,
          "global_phase": 0, "instructions": operations or [],
          "measurement_mapping": [{"qubit": 0, "clbit": 0}, {"qubit": 1, "clbit": 1}]}
    return CompiledArtifact(route, "offline-device", fmt, payload, ir, snapshot or {})


def test_rigetti_actual_execution_models_and_transport_signatures():
    api = sdk("rigetti", "qcs_sdk.qpu.api")
    translation = importlib.import_module("qcs_sdk.qpu.translation")
    memory_type = importlib.import_module("qcs_sdk.qpu").MemoryValues
    inspect.signature(translation.translate).bind(
        native_quil="DECLARE ro BIT[1]\nRX(pi/2) 0\nMEASURE 0 ro[0]\n",
        num_shots=7, quantum_processor_id="offline", client=None)
    inspect.signature(api.submit).bind(program="offline", patch_values={}, quantum_processor_id="offline", client=None)
    inspect.signature(api.retrieve_results).bind("offline", quantum_processor_id="offline", client=None)
    inspect.signature(api.cancel_job).bind("offline", quantum_processor_id="offline", client=None)
    result = api.ExecutionResults(
        buffers={"ro0": api.ExecutionResult([0, 1]), "iq": api.ExecutionResult([complex(0.25, -0.5)])},
        memory={"ro": memory_type.Binary([1, 0])}, execution_duration_microseconds=41)
    assert list(result.buffers["ro0"].data) == [0, 1]
    assert list(result.buffers["iq"].data) == [complex(0.25, -0.5)]
    assert result.memory["ro"].inner() == [1, 0]
    assert result.execution_duration_microseconds == 41


def test_iqm_actual_circuit_objects_validate_native_adapter_output():
    client_type = sdk("iqm", "iqm.iqm_client").IQMClient
    builder = importlib.import_module("iqm.pulse.builder")
    circuit_type = importlib.import_module("iqm.pulse.circuit_operations").Circuit
    inspect.signature(client_type).bind(iqm_server_url="https://example.invalid", quantum_computer="offline",
                                       token="offline-test-token", client_signature="qstack")
    snapshot = {"qubit_labels": ["QB1", "QB2"], "calibration_set_id": str(UUID(int=1))}
    operations = [{"op": "u1q", "qubits": [0], "params": [math.pi / 2, 0.125]},
                  {"op": "cz", "qubits": [0, 1]},
                  {"op": "measure", "qubits": [0], "clbits": [0]},
                  {"op": "measure", "qubits": [1], "clbits": [1]}]
    value = artifact("iqm", "iqm-json", "", operations, snapshot)
    value.format, value.payload = serialize_native("iqm", value.physical_ir, snapshot)
    captured = []
    def submit(circuits, **kwargs):
        inspect.signature(client_type.submit_circuits).bind(object(), circuits, **kwargs)
        circuit = circuits[0]
        assert isinstance(circuit, circuit_type)
        assert all(isinstance(op, builder.CircuitOperation) for op in circuit.instructions)
        circuit.validate(builder.build_quantum_ops({}))
        captured.append((circuit, kwargs))
        return NS(job_id=UUID(int=2))
    adapter = get_adapter("iqm", {"_client": NS(submit_circuits=submit)})
    receipt = adapter.submit(value, SubmissionOptions(mode="live", shots=7, name="offline"))
    circuit, kwargs = captured[0]
    assert circuit.instructions[0].args == {"angle": math.pi / 2, "phase": 0.125}
    assert circuit.instructions[1].locus == ("QB1", "QB2")
    assert [op.args["key"] for op in circuit.instructions[2:]] == ["c0", "c1"]
    assert kwargs == {"shots": 7, "calibration_set_id": UUID(int=1)}
    assert receipt.job_id == str(UUID(int=2))


def test_oqc_actual_task_serialization_disables_tket_and_requests_counts():
    client = sdk("oqc", "qcaas_client.client")
    importlib.import_module("compiler_config.config")
    captured = []
    def submit(task, *, qpu_id):
        assert isinstance(task, client.QPUTask)
        inspect.signature(client.OQCClient.schedule_tasks).bind(object(), task, qpu_id=qpu_id)
        captured.append(task.to_json())
        return [NS(task_id="offline")]
    adapter = get_adapter("oqc", {"device": "offline", "_client": NS(schedule_tasks=submit)})
    adapter.submit(artifact("oqc", "openqasm3", "OPENQASM 3.0;"), SubmissionOptions(mode="live", shots=7))
    data = captured[0]
    assert data["qpu_id"] == "offline" and data["program"] == "OPENQASM 3.0;"
    config = json.loads(data["config"])["$data"]
    assert config["repeats"] == 7
    assert config["optimizations"]["$data"]["tket_optimizations"]["$value"] == 1
    assert config["results_format"]["$data"]["format"]["$value"] == 1
    assert config["results_format"]["$data"]["transforms"]["$value"] == 3


def test_aqt_actual_arnica_request_model_and_isolated_config(tmp_path, monkeypatch):
    # The connector creates a default config at import. Keep even that read
    # isolated from the developer's real credential directory/environment.
    monkeypatch.setattr(Path, "home", classmethod(lambda cls: tmp_path))
    for key in tuple(os.environ):
        if key.startswith("AQT_"):
            monkeypatch.delenv(key)
    connector = sdk("aqt", "aqt_connector")
    request_type = importlib.import_module("aqt_connector.models.arnica.request_bodies.jobs").SubmitJobRequest
    config_dir = tmp_path / "arnica"
    config_dir.mkdir()
    (config_dir / "config").write_text('[default]\narnica_url="https://example.invalid/api"\nclient_id="offline-client"\n')
    config = connector.ArnicaConfig(config_dir)
    assert config.arnica_url == "https://example.invalid/api" and config.client_id == "offline-client"
    explicit_file = config_dir / "custom-credentials.toml"
    explicit_file.write_text('[default]\narnica_url="https://custom.invalid/api"\nclient_id="custom-client"\nstore_access_token=false\n')
    def environment_digest():
        return hashlib.sha256(json.dumps(dict(os.environ), sort_keys=True).encode()).hexdigest()
    before_environment = environment_digest()
    app = get_adapter("aqt", {"credentials_file": str(explicit_file)}).app()
    try:
        assert app.config.arnica_url == "https://custom.invalid/api"
        assert app.config.client_id == "custom-client" and app.config.store_access_token is False
        assert app.config._app_dir == config_dir
        assert environment_digest() == before_environment
    finally:
        app.close()
    inspect.signature(connector.log_in).bind(object())
    inspect.signature(connector.get_access_token).bind(object())
    captured = []
    def transport(method, url, *, headers, data, files, text):
        assert method == "POST" and url.endswith("/v1/submit/offline/offline-device")
        request = request_type.model_validate(data)
        captured.append(request.model_dump(mode="json"))
        return {"job": {"job_id": "offline"}}
    adapter = get_adapter("aqt", {"api_key": "offline-test-token", "workspace": "offline", "_transport": transport})
    operations = [{"op": "u1q", "qubits": [0], "params": [math.pi / 2, math.pi]},
                  {"op": "rz", "qubits": [0], "params": [-math.pi / 3]},
                  {"op": "rxx", "qubits": [0, 1], "params": [math.pi / 4]},
                  {"op": "measure", "qubits": [0], "clbits": [0]},
                  {"op": "measure", "qubits": [1], "clbits": [1]}]
    adapter.submit(artifact("aqt", "aqt-native", "", operations), SubmissionOptions(mode="live", shots=7))
    circuit = captured[0]["payload"]["circuits"][0]
    assert circuit["repetitions"] == 7 and circuit["number_of_qubits"] == 2
    assert circuit["quantum_circuit"][0] == {"operation": "R", "qubit": 0, "theta": 0.5, "phi": 1.0}
    assert circuit["quantum_circuit"][2] == {"operation": "RXX", "qubits": [0, 1], "theta": 0.25}
    assert circuit["quantum_circuit"][-1] == {"operation": "MEASURE"}
