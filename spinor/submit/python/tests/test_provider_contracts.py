"""Offline tests of official provider wire/SDK contracts; never contact a service."""
from __future__ import annotations

import base64
import json
import math
import os
import sys
from types import SimpleNamespace as NS
from uuid import UUID

import pytest

from qstack.models import CompiledArtifact, JobReceipt, QStackError, SubmissionOptions
from qstack.providers import ADAPTERS, UNAVAILABLE, get_adapter
from qstack.providers import base, cloud, specialized, native, qibolab, isolated


def artifact(route, fmt="native", payload="", ops=None):
    ir = {"schema_version": 1, "target": "test-device", "name": "test", "num_qubits": 2,
          "num_clbits": 2, "global_phase": 0, "instructions": ops or [],
          "measurement_mapping": [{"qubit": 0, "clbit": 1}, {"qubit": 1, "clbit": 0}]}
    return CompiledArtifact(route, "test-device", fmt, payload, ir)


class Wire:
    def __init__(self, *responses):
        self.calls = []
        self.responses = list(responses)

    def __call__(self, method, url, **kwargs):
        self.calls.append((method, url, kwargs))
        assert self.responses, "Unexpected network request"
        return self.responses.pop(0)


def modules(monkeypatch, mapping):
    def resolve(module, extra):
        assert module in mapping, f"Unexpected SDK import: {module}"
        return mapping[module]
    for module in (cloud, specialized, native, qibolab):
        monkeypatch.setattr(module, "optional", resolve)


def test_factory_has_all_routes_without_loading_sdks():
    assert set(ADAPTERS) == {"ibm", "google", "quantinuum", "azure", "aws", "ionq", "rigetti", "iqm", "oqc", "aqt", "anyon", "alicebob", "qibolab"}
    for route in ADAPTERS:
        adapter = get_adapter(route, {})
        assert adapter.route == route
        for method in ("check_auth", "discover", "login", "submit", "status", "results", "cancel"):
            assert callable(getattr(adapter, method))
    for route in UNAVAILABLE:
        adapter = get_adapter(route)
        assert adapter.check_auth()["available"] is False
        with pytest.raises(QStackError, match="public|laboratory|hardware|cloud"):
            adapter.submit(None, None)


def test_ionq_v04_native_submit_artifacts_cancel_and_auth():
    wire = Wire({"key_id": "opaque"}, {"id": "j1"},
                {"id": "j1", "status": "completed", "results": {"ionq.result.probabilities.json.v2": "a1"}},
                {"0": 0.4, "3": 0.6}, {"id": "j1", "status": "canceled"})
    adapter = get_adapter("ionq", {"api_key": "test-secret", "device": "qpu.forte-1", "_transport": wire})
    assert adapter.check_auth()["authenticated"]
    source = {"qubits": 2, "gateset": "native", "circuit": [{"gate": "zz", "targets": [0, 1], "angle": 0.125}]}
    receipt = adapter.submit(artifact("ionq", "ionq-native-json", json.dumps(source)), SubmissionOptions(mode="live", shots=7))
    method, url, request = wire.calls[1]
    assert (method, url) == ("POST", "https://api.ionq.co/v0.4/jobs")
    assert request["headers"]["Authorization"] == "apiKey test-secret"
    assert request["data"] == {"type": "ionq.circuit.v1", "backend": "qpu.forte-1", "shots": 7, "name": "qstack", "input": source,
                               "settings": {"error_mitigation": {"debiasing": False}}}
    assert "test-secret" not in json.dumps(receipt.to_dict())
    result = adapter.results(receipt)
    assert result.counts is None
    assert result.raw["artifacts"]["ionq.result.probabilities.json.v2"] == [{"0": 0.4, "3": 0.6}]
    adapter.cancel(receipt)
    assert wire.calls[-1][:2] == ("PUT", "https://api.ionq.co/v0.4/jobs/j1/status/cancel")


def test_ionq_rejects_qis_and_http_before_network():
    wire = Wire()
    adapter = get_adapter("ionq", {"api_key": "secret", "_transport": wire})
    with pytest.raises(QStackError, match="gateset=native"):
        adapter.submit(artifact("ionq", "ionq-native-json", '{"gateset":"qis"}'), SubmissionOptions(mode="live"))
    adapter.config["url"] = "http://insecure.example"
    with pytest.raises(QStackError, match="HTTPS"):
        adapter.check_auth()
    assert not wire.calls


def test_anyon_wire_auth_realm_and_actual_histogram():
    wire = Wire({"job": {"id": "a1"}}, {"job": {"status": {"type": "SUCCEEDED"}}, "result": {"histogram": {"01": 3}}})
    adapter = get_adapter("anyon", {"url": "https://anyon.example", "user": "test-user", "access_token": "secret", "realm": "r", "project": "p", "_transport": wire})
    circuit = {"qubitCount": 2, "bitCount": 2, "operations": [{"type": "readout", "qubits": [0], "bits": [1]}]}
    receipt = adapter.submit(artifact("anyon", "anyon-json", json.dumps(circuit)), SubmissionOptions(mode="live", shots=3))
    request = wire.calls[0][2]
    assert request["headers"]["Authorization"] == "Basic " + base64.b64encode(b"test-user:secret").decode()
    assert request["headers"]["X-Realm"] == "r"
    assert request["data"]["projectID"] == "p"
    assert request["data"]["circuit"] == circuit
    assert adapter.results(receipt).counts == {"01": 3}
    with pytest.raises(QStackError, match="cancellation"):
        adapter.cancel(receipt)


def test_felis_two_phase_upload_and_cancellation():
    wire = Wire({"id": "ab1"}, {}, "00,3\n", {})
    adapter = get_adapter("alicebob", {"api_key": "already-encoded", "_transport": wire})
    receipt = adapter.submit(artifact("alicebob", "qir-text", "define void @main() {}"), SubmissionOptions(mode="live", shots=3))
    assert wire.calls[0][2]["headers"]["Authorization"] == "Basic already-encoded"
    assert wire.calls[0][2]["data"]["inputDataFormat"] == "HUMAN_QIR"
    assert wire.calls[0][2]["data"]["inputParams"] == {"nbShots": 3}
    assert wire.calls[1][2]["files"] == {"input": "define void @main() {}"}
    assert adapter.results(receipt).raw == {"output": "00,3\n"}
    adapter.cancel(receipt)
    assert wire.calls[-1][:2] == ("DELETE", "https://api-gcp.alice-bob.com/v1/jobs/ab1")


def test_aqt_current_native_contract_and_classical_mapping():
    wire = Wire({"job": {"job_id": "aq1"}}, {"response": {"status": "finished", "result": {"0": [[1, 0], [1, 0], [0, 1]]}}})
    adapter = get_adapter("aqt", {"api_key": "token", "workspace": "w", "_transport": wire})
    ops = [{"op": "u1q", "qubits": [0], "params": [math.pi / 2, math.pi]},
           {"op": "rxx", "qubits": [0, 1], "params": [math.pi / 4]},
           {"op": "rz", "qubits": [1], "params": [-math.pi / 2]},
           {"op": "measure", "qubits": [0], "clbits": [1]},
           {"op": "measure", "qubits": [1], "clbits": [0]}]
    receipt = adapter.submit(artifact("aqt", ops=ops), SubmissionOptions(mode="live", shots=3))
    request = wire.calls[0][2]
    assert request["headers"]["Authorization"] == "Bearer token"
    assert wire.calls[0][1] == "https://arnica.aqt.eu/api/v1/submit/w/test-device"
    assert request["data"]["payload"]["circuits"][0]["quantum_circuit"] == [
        {"operation": "R", "qubit": 0, "theta": 0.5, "phi": 1},
        {"operation": "RXX", "qubits": [0, 1], "theta": 0.25},
        {"operation": "RZ", "qubit": 1, "phi": -0.5}, {"operation": "MEASURE"}]
    assert adapter.results(receipt).counts == {"10": 2, "01": 1}


@pytest.mark.parametrize("ops,message", [
    ([{"op": "rxx", "qubits": [0, 1], "params": [-0.1]}], "RXX"),
    ([{"op": "if", "then": []}], "control flow"),
    ([{"op": "measure"}, {"op": "rz", "qubits": [0], "params": [1]}], "terminal"),
])
def test_aqt_rejects_non_native_semantics(ops, message):
    with pytest.raises(QStackError, match=message):
        get_adapter("aqt", {}).submit(artifact("aqt", ops=ops), SubmissionOptions(mode="live"))


class QC:
    def __init__(self, nq, nc, name):
        self.ops = []
        self.num_qubits, self.num_clbits = nq, nc

    def __getattr__(self, op):
        def append(*args, **kwargs):
            self.ops.append((op, args, kwargs))
        return append


def test_ibm_native_circuit_sampler_and_resume(monkeypatch):
    calls = []
    backend = NS(name="ibm-test", target=NS(instruction_supported=lambda **kwargs: True))
    pub = NS(data={"c": NS(get_counts=lambda: {"01": 4})}, metadata={})
    job = NS(job_id=lambda: "ibm1", status=lambda: "DONE", result=lambda: [pub], cancel=lambda: None)
    service = NS(backend=lambda name: backend, job=lambda identifier: job)
    class Sampler:
        def __init__(self, *, mode):
            assert mode is backend
        def run(self, pubs, *, shots):
            calls.append((pubs, shots))
            return job
    modules(monkeypatch, {"qiskit": NS(QuantumCircuit=QC), "qiskit_ibm_runtime": NS(SamplerV2=Sampler)})
    ops = [{"op": "rz", "qubits": [0], "params": [0.25]}, {"op": "sx", "qubits": [0]},
           {"op": "cx", "qubits": [0, 1]}, {"op": "measure", "qubits": [0], "clbits": [1]}]
    adapter = get_adapter("ibm", {"_client": service})
    receipt = adapter.submit(artifact("ibm", ops=ops), SubmissionOptions(mode="live", shots=4))
    assert calls[0][0][0].ops == [("rz", (0.25, 0), {}), ("sx", (0,), {}), ("cx", (0, 1), {}), ("measure", (0, 1), {})]
    assert adapter.status(receipt)["status"] == "DONE"
    assert adapter.results(receipt).counts == {"01": 4}
    assert adapter.cancel(receipt)["cancellation_requested"]


def test_braket_requires_native_verbatim_and_disables_rewiring(monkeypatch):
    calls = []
    class Device:
        def __init__(self, arn, *, aws_session):
            assert arn == "test-device" and aws_session == "session"
        def run(self, program, *, shots, disable_qubit_rewiring):
            calls.append((program.source, shots, disable_qubit_rewiring))
            return NS(id="arn:task:test")
    modules(monkeypatch, {"braket.aws": NS(AwsDevice=Device), "braket.ir.openqasm": NS(Program=lambda *, source: NS(source=source))})
    adapter = get_adapter("aws", {"_client": "session"})
    with pytest.raises(QStackError, match="verbatim"):
        adapter.submit(artifact("aws", "openqasm3", "OPENQASM 3.0;"), SubmissionOptions(mode="live"))
    source = "OPENQASM 3.0;\n#pragma braket verbatim\nbox { x $0; }"
    receipt = adapter.submit(artifact("aws", "openqasm3", source), SubmissionOptions(mode="live", shots=4))
    assert receipt.job_id == "arn:task:test"
    assert calls == [(source, 4, True)]


def test_google_native_grid_serialization_and_required_config(monkeypatch):
    calls, ops = [], []
    class Circuit:
        def append(self, op, *, strategy):
            ops.append((op, strategy))
    cirq = NS(Circuit=Circuit, GridQubit=lambda row, col: (row, col),
              rz=lambda angle: lambda q: ("rz", angle, q), CZ=lambda *q: ("cz", q),
              measure=lambda q, *, key: ("measure", q, key), InsertStrategy=NS(NEW="new"))
    def run_sweep(program, *, device_config_name, repetitions, program_description):
        calls.append((device_config_name, repetitions))
        return NS(job_id="g1", program_id="p1")
    processor = NS(get_device=lambda: NS(validate_circuit=lambda c: None), run_sweep=run_sweep)
    modules(monkeypatch, {"cirq": cirq})
    adapter = get_adapter("google", {"_client": NS(get_processor=lambda d: processor), "qubit_labels": [[5, 4], [5, 5]], "device_config_name": "rainbow"})
    source = artifact("google", ops=[{"op": "rz", "qubits": [0], "params": [0.3]}, {"op": "cz", "qubits": [0, 1]}, {"op": "measure", "qubits": [1], "clbits": [0]}])
    receipt = adapter.submit(source, SubmissionOptions(mode="live", shots=3))
    assert receipt.metadata["program_id"] == "p1"
    assert ops == [(("rz", 0.3, (5, 4)), "new"), (("cz", ((5, 4), (5, 5))), "new"), (("measure", (5, 5), "c0"), "new")]
    assert calls == [("rainbow", 3)]


def test_rigetti_native_translation_and_resumable_raw_readout(monkeypatch):
    calls = []
    def translate(*, native_quil, num_shots, quantum_processor_id, client):
        calls.append(("translate", native_quil, num_shots, client))
        return NS(program="encrypted-binary", ro_sources={"ro[0]": "q0"})
    def submit(*, program, patch_values, quantum_processor_id, client):
        assert program == "encrypted-binary" and patch_values == {}
        return "r1"
    def retrieve(job_id, *, quantum_processor_id, client):
        return NS(buffers={"q0": NS(data=[0, 1], shape=[2], dtype="int")}, memory={}, execution_duration_microseconds=3)
    modules(monkeypatch, {"qcs_sdk.qpu.translation": NS(translate=translate), "qcs_sdk.qpu.api": NS(submit=submit, retrieve_results=retrieve)})
    adapter = get_adapter("rigetti", {"_client": "oauth"})
    receipt = adapter.submit(artifact("rigetti", "quil", "DECLARE ro BIT[1]\nRX(pi/2) 0\nMEASURE 0 ro[0]"), SubmissionOptions(mode="live", shots=2))
    assert calls[0][0] == "translate"
    assert adapter.results(receipt).raw["ro_sources"] == {"ro[0]": "q0"}
    with pytest.raises(QStackError, match="nonblocking"):
        adapter.status(receipt)


def test_iqm_current_circuit_model_radians_calibration_and_uuid(monkeypatch):
    captured = []
    jid = UUID("00000000-0000-0000-0000-000000000001")
    def submit(circuits, *, shots):
        captured.extend(circuits)
        assert shots == 5
        return NS(job_id=jid)
    modules(monkeypatch, {"iqm.pulse.builder": NS(CircuitOperation=lambda **kw: NS(**kw)),
                         "iqm.pulse.circuit_operations": NS(Circuit=lambda **kw: NS(**kw))})
    client = NS(submit_circuits=submit, get_job=lambda identifier: NS(update=lambda: "completed", result=lambda: {"measurements": {"c": [[1], [0]]}}))
    adapter = get_adapter("iqm", {"_client": client})
    source = {"name": "test", "instructions": [{"name": "prx", "locus": ["QB1"], "args": {"angle": 0.2, "phase": 0.4}}]}
    receipt = adapter.submit(artifact("iqm", "iqm-json", json.dumps(source)), SubmissionOptions(mode="live", shots=5))
    assert captured[0].instructions[0].args == {"angle": 0.2, "phase": 0.4}
    assert captured[0].instructions[0].locus == ("QB1",)
    assert adapter.results(receipt).raw["measurements"] == {"c": [[1], [0]]}


def test_oqc_disables_tket_and_base64_encodes_bitcode(monkeypatch):
    captured = []
    class Tket:
        def disable(self):
            self.disabled = True
    def schedule(task, *, qpu_id):
        captured.append(task)
        return [NS(task_id="o1")]
    modules(monkeypatch, {"qcaas_client.client": NS(QPUTask=lambda **kw: NS(**kw)),
        "compiler_config.config": NS(Tket=Tket, CompilerConfig=lambda **kw: NS(**kw),
                                     QuantumResultsFormat=lambda: NS(binary_count=lambda: "binary"))})
    adapter = get_adapter("oqc", {"_client": NS(schedule_tasks=schedule)})
    adapter.submit(artifact("oqc", "qir-bitcode", b"BC\xc0\xde"), SubmissionOptions(mode="live", shots=6))
    assert captured[0].program == base64.b64encode(b"BC\xc0\xde").decode()
    assert captured[0].config.optimizations.disabled
    assert captured[0].config.repeats == 6


def test_quantinuum_upload_then_execute_without_compile():
    calls = []
    def upload(*, qir, name, project):
        calls.append(("upload", qir, project))
        return "qir-ref"
    def execute(**kw):
        calls.append(("execute", kw))
        return NS(id="q1")
    qnx = NS(projects=NS(get=lambda *, name: "project-ref"), qir=NS(upload=upload),
             models=NS(QuantinuumConfig=lambda **kw: kw), start_execute_job=execute)
    adapter = get_adapter("quantinuum", {"_client": qnx, "project": "compiler", "device": "H2-1E"})
    adapter.submit(artifact("quantinuum", "qir-bitcode", b"BC\xc0\xde"), SubmissionOptions(mode="live", shots=9))
    assert calls[1][1]["programs"] == ["qir-ref"]
    assert calls[1][1]["n_shots"] == [9]
    assert calls[1][1]["backend_config"] == {"device_name": "H2-1E", "no_opt": True,
        "allow_implicit_swaps": False, "allow_2q_gate_rebase": False, "simplify_initial": False}
    assert "max_cost" not in calls[1][1]


def test_azure_target_specific_qir_and_cancel_job_object():
    calls = []
    def submit(**kw):
        calls.append(kw)
        return NS(id="az1")
    target = NS(provider_id="quantinuum", submit=submit)
    job = NS(id="az1")
    client = NS(get_targets=lambda *, name: target, get_job=lambda identifier: job,
                cancel_job=lambda supplied: calls.append(supplied))
    adapter = get_adapter("azure", {"_client": client})
    receipt = adapter.submit(artifact("azure", "qir-bitcode", b"BC\xc0\xde"), SubmissionOptions(mode="live", shots=3))
    assert calls[0]["input_data_format"] == "qir.v1"
    assert calls[0]["input_data"] == b"BC\xc0\xde"
    assert calls[0]["input_params"]["entryPoint"] == "main"
    adapter.config["entry_point"] = "wrong"
    with pytest.raises(QStackError, match="entry point"):
        adapter.submit(artifact("azure", "qir-bitcode", b"BC"), SubmissionOptions(mode="live"))
    assert len(calls) == 1
    adapter.cancel(receipt)
    assert calls[-1] is job


def test_no_provider_adapter_accepts_cassette_as_live():
    for route in ADAPTERS:
        with pytest.raises(QStackError, match="live mode"):
            get_adapter(route).submit(artifact(route), SubmissionOptions(mode="cassette"))


def test_native_json_serialization_keeps_units_and_physical_labels():
    ir = artifact("ionq", ops=[{"op": "gpi2", "qubits": [1], "params": [math.pi]},
        {"op": "rzz", "qubits": [1, 0], "params": [math.pi / 2]},
        {"op": "measure", "qubits": [1], "clbits": [0]}]).physical_ir
    fmt, text = native.serialize_native("ionq", ir)
    assert fmt == "ionq-native-json"
    assert json.loads(text)["circuit"] == [{"gate": "gpi2", "target": 1, "phase": 0.5},
        {"gate": "zz", "targets": [1, 0], "angle": 0.25}]
    ir["instructions"] = [{"op": "u1q", "qubits": [1], "params": [0.2, 0.4]},
        {"op": "cz", "qubits": [1, 0]}, {"op": "measure", "qubits": [0], "clbits": [1]}]
    with pytest.raises(QStackError, match="qubit_labels"):
        native.serialize_native("iqm", ir)
    _, text = native.serialize_native("iqm", ir, {"qubit_labels": ["QB7", "QB2"]})
    assert json.loads(text)["instructions"] == [
        {"name": "prx", "locus": ["QB2"], "args": {"angle": 0.2, "phase": 0.4}},
        {"name": "cz", "locus": ["QB2", "QB7"], "args": {}},
        {"name": "measure", "locus": ["QB7"], "args": {"key": "c1"}}]
    ir["instructions"] = [{"op": "sx", "qubits": [1]}, {"op": "rz", "qubits": [0], "params": [0.7]},
        {"op": "cz", "qubits": [1, 0]}, {"op": "measure", "qubits": [0], "clbits": [1]}]
    _, text = native.serialize_native("anyon", ir)
    assert json.loads(text)["operations"] == [
        {"type": "x_90", "qubits": [1], "parameters": {}},
        {"type": "p", "qubits": [0], "parameters": {"lambda": 0.7}},
        {"type": "cz", "qubits": [1, 0], "parameters": {}},
        {"type": "readout", "qubits": [0], "bits": [1]}]


@pytest.mark.parametrize("route", ["ionq", "anyon", "iqm"])
def test_native_json_rejects_control_flow_and_nonfinite_parameters(route):
    ir = artifact(route, ops=[{"op": "if", "clbits": [0], "condition_value": 1}]).physical_ir
    with pytest.raises(QStackError, match="control flow"):
        native.serialize_native(route, ir, {"qubit_labels": ["QB1", "QB2"]})
    ir["instructions"] = [{"op": "rz", "qubits": [0], "params": [float("nan")]}]
    with pytest.raises(QStackError, match="finite"):
        native.serialize_native(route, ir, {"qubit_labels": ["QB1", "QB2"]})


def test_ibm_nested_branches_preserve_scoped_phase_and_order():
    from contextlib import contextmanager
    class DynamicQC(QC):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.clbits = ["c0", "c1"]
            self.phases = [0]
        @property
        def global_phase(self):
            return self.phases[-1]
        @global_phase.setter
        def global_phase(self, value):
            self.phases[-1] = value
        @contextmanager
        def scope(self, label):
            self.ops.append(("begin", label))
            self.phases.append(0)
            yield
            self.ops.append(("end", label, self.phases.pop()))
        @contextmanager
        def if_test(self, condition):
            with self.scope(("if", condition)):
                yield self.scope("else")
    ops = [{"op": "measure", "qubits": [0], "clbits": [1]},
        {"op": "if", "clbits": [1], "condition_value": 0},
        {"op": "gphase", "params": [0.25]}, {"op": "x", "qubits": [1]},
        {"op": "if", "clbits": [0], "condition_value": 1}, {"op": "rz", "qubits": [1], "params": [0.5]},
        {"op": "endif"}, {"op": "else"}, {"op": "reset", "qubits": [1]}, {"op": "endif"}]
    qc = native.qiskit_circuit(artifact("ibm", ops=ops), module=NS(QuantumCircuit=DynamicQC))
    assert qc.global_phase == 0
    assert ("end", ("if", ("c1", 0)), 0.25) in qc.ops
    assert qc.ops.index(("x", (1,), {})) < qc.ops.index(("begin", "else")) < qc.ops.index(("reset", (1,), {}))
    for broken in ([{"op": "endif"}], [{"op": "if", "clbits": [0], "condition_value": 1}]):
        with pytest.raises(QStackError, match="marker|endif"):
            native.qiskit_circuit(artifact("ibm", ops=broken), module=NS(QuantumCircuit=DynamicQC))


def test_rest_discovery_uses_actual_capabilities_and_disabled_edges():
    wire = Wire([{"backend": "qpu.forte-1", "qubits": 36, "supported_native_gates": ["gpi", "gpi2", "zz"]}])
    record = get_adapter("ionq", {"api_key": "x", "_transport": wire}).discover()[0]
    assert record["capability_verified"] and record["all_to_all"] and "rzz" in record["native_gates"]
    wire = Wire({"items": [{"name": "yukon", "qubitCount": 6, "connectivity": "linear",
                            "disconnectedQubits": [4], "disconnectedConnections": [[1, 2]]}]})
    record = get_adapter("anyon", {"host": "https://example.test", "user": "u", "access_token": "t", "_transport": wire}).discover()[0]
    assert record["coupling"] == [[0, 1], [2, 3]]
    assert record["unavailable_qubits"] == [4]
    wire = Wire([{"name": "boson4", "numQubits": 1, "instructions": [
        {"signature": "void __quantum__qis__x__body(%Qubit*)"}, {"signature": "void __quantum__qis__mz__body(%Qubit*, %Result*)"}]}])
    record = get_adapter("alicebob", {"api_key": "encoded", "_transport": wire}).discover()[0]
    assert record["capability_verified"] and record["native_gates"] == ["measure", "x"]


def test_google_discovery_preserves_grid_connectivity():
    spec = NS(valid_qubits=["5_4", "5_5", "6_4"],
        valid_gates=[NS(WhichOneof=lambda oneof, name=name: name) for name in ("syc", "phased_xz", "meas")],
        valid_targets=[NS(target_ordering=1, targets=[NS(ids=["5_4", "5_5"]), NS(ids=["5_4", "6_4"])])])
    processor = NS(processor_id="rainbow", get_device_specification=lambda: spec)
    record = get_adapter("google", {"_client": NS(list_processors=lambda: [processor])}).discover()[0]
    assert record["qubit_labels"] == ["5_4", "5_5", "6_4"] and record["coupling"] == [[0, 1], [0, 2]]
    assert "syc" in record["native_gates"] and "u1q" in record["native_gates"]
    assert record["capability_verified"] and not record["supports"]["feedforward"]


def test_rigetti_discovery_uses_instruction_sites_not_unavailable_architecture_edges(monkeypatch):
    isa = NS(instructions=[NS(name="RX", sites=[NS(node_ids=[2]), NS(node_ids=[5])]),
                           NS(name="CZ", sites=[NS(node_ids=[2, 5])]), NS(name="XY", sites=[])], json=lambda: "{}")
    modules(monkeypatch, {"qcs_sdk.qpu": NS(list_quantum_processors=lambda *, client: ["Ankaa"]),
        "qcs_sdk.qpu.isa": NS(get_instruction_set_architecture=lambda name, *, client: isa)})
    record = get_adapter("rigetti", {"_client": "profile"}).discover()[0]
    assert record["available_qubits"] == [2, 5] and record["qubits"] == 6
    assert record["native_gates"] == ["cz", "rx"] and record["gate_loci"]["cz"] == [[2, 5]]


def test_quantinuum_discovery_includes_helios_and_current_qir_submission():
    architecture = NS(nodes=[], n_nodes=98, fully_connected=True, edges=[])
    info = NS(device=architecture, supports_reset=True, supports_midcircuit_measurement=True, supports_fast_feedforward=True)
    device = NS(device_name="Helios-1", backend_name="HeliosBackend", stored_backend_info=info)
    calls = []
    qnx = NS(devices=NS(get_all=lambda: [device]), projects=NS(get=lambda *, id: calls.append(id) or "project"),
        qir=NS(upload=lambda **kw: calls.append(kw) or "ref"),
        models=NS(HeliosConfig=lambda **kw: kw), start_execute_job=lambda **kw: calls.append(kw) or NS(id="h1"))
    adapter = get_adapter("quantinuum", {"_client": qnx, "device": "Helios-1", "project": "00000000-0000-0000-0000-000000000001"})
    record = adapter.discover()[0]
    assert record["qubits"] == 98 and record["supports"]["feedforward"] and "qir-bitcode" in record["formats"]
    adapter.submit(artifact("quantinuum", "qir-bitcode", b"BC"), SubmissionOptions(mode="live"))
    assert isinstance(calls[0], UUID) and calls[1]["qir"] == b"BC"
    assert calls[2]["backend_config"] == {"system_name": "Helios-1"}


def test_azure_discovery_does_not_invent_unknown_targets():
    names = ["quantinuum.qpu.h2-1", "quantinuum.sim.h2-1e", "ionq.qpu.forte-1", "future.unknown"]
    targets = [NS(name=n, provider_id=n.split(".")[0], input_data_format="provider.format", output_data_format="result") for n in names]
    records = get_adapter("azure", {"_client": NS(get_targets=lambda: targets)}).discover()
    assert [r.get("qubits") for r in records] == [56, 32, 36, None]
    assert records[2]["formats"] == ["ionq-native-json"]
    assert not records[-1]["capability_verified"]


def test_config_aliases_are_explicit_and_do_not_mutate_environment(monkeypatch):
    before = dict(os.environ)
    calls = []
    modules(monkeypatch, {"iqm.iqm_client": NS(IQMClient=lambda **kw: calls.append(kw) or "iqm"),
        "qcaas_client.client": NS(OQCClient=lambda **kw: calls.append(kw) or "oqc"),
        "qdk.azure": NS(Workspace=NS(from_connection_string=lambda value: calls.append(value) or "azure"))})
    assert get_adapter("iqm", {"url": "https://iqm.test", "quantum_computer": "garnet", "tokens_file": "custom.tokens"}).client() == "iqm"
    assert calls[-1]["tokens_file"] == "custom.tokens" and calls[-1]["quantum_computer"] == "garnet"
    assert get_adapter("oqc", {"url": "https://oqc.test", "access_token": "x"}).client() == "oqc"
    assert calls[-1]["authentication_token"] == "x"
    assert get_adapter("azure", {"connection_string": "test-connection"}).client() == "azure"
    assert dict(os.environ) == before


def test_custom_qcs_sdk_files_are_scoped_to_subprocess_and_credentials_not_argv(monkeypatch):
    before, calls = dict(os.environ), []
    def run(command, **kw):
        calls.append((command, kw))
        assert kw["env"]["QCS_SETTINGS_FILE_PATH"] == "custom/settings.toml"
        assert kw["env"]["QCS_SECRETS_FILE_PATH"] == "custom/secrets.toml"
        request = json.loads(kw["input"])
        assert request["arguments"]["artifact"]["payload"] == base64.b64encode(b"DECLARE ro BIT[1]").decode()
        return NS(returncode=0, stdout=json.dumps({"value": JobReceipt("rigetti", "test-device", "r1").to_dict()}))
    monkeypatch.setattr(isolated.subprocess, "run", run)
    adapter = get_adapter("rigetti", {"sdk_config_file": "custom/settings.toml", "tokens_file": "custom/secrets.toml", "api_key": "secret"})
    assert adapter.submit(artifact("rigetti", "quil", "DECLARE ro BIT[1]"), SubmissionOptions(mode="live")).job_id == "r1"
    assert all("secret" not in arg for arg in calls[0][0]) and dict(os.environ) == before


def test_qibolab_configured_bridge_runs_calibrated_sequences_and_disconnects(monkeypatch):
    calls = []
    modules(monkeypatch, {"qibolab": NS(AcquisitionType=NS(DISCRIMINATION="disc"), AveragingMode=NS(SINGLESHOT="single"))})
    platform = NS(connect=lambda: calls.append("connect"), disconnect=lambda: calls.append("disconnect"),
        execute=lambda sequences, **kw: calls.append((sequences, kw)) or {"readout-id": [[1], [0]]})
    bridge = {"platform": platform, "snapshot": {"device": "lab", "capability_verified": True},
              "build_sequences": lambda artifact: ["calibrated-sequence"]}
    adapter = get_adapter("qibolab", {"_bridge": bridge})
    assert adapter.discover()[0]["capability_verified"]
    receipt = adapter.submit(artifact("qibolab"), SubmissionOptions(mode="live", shots=2))
    assert calls[0] == "connect" and calls[-1] == "disconnect" and calls[1][1]["nshots"] == 2
    assert adapter.status(receipt)["status"] == "COMPLETED"
    assert adapter.results(receipt).raw == {"readout-id": [[1], [0]]}


def test_provider_statuses_are_normalized_for_resumable_waiting():
    from enum import Enum
    class State(str, Enum):
        COMPLETE = "COMPLETED"
    assert base.plain(State.COMPLETE) == "COMPLETED" and type(base.plain(State.COMPLETE)) is str
    receipt = JobReceipt("aqt", "t", "j")
    adapter = get_adapter("aqt", {"api_key": "x", "_transport": Wire({"response": {"status": "finished"}})})
    assert adapter.status(receipt)["status"] == "completed"
    adapter = get_adapter("anyon", {"host": "https://anyon.test", "user": "u", "access_token": "x", "_transport": Wire({"job": {"status": {"type": "SUCCEEDED"}}})})
    assert adapter.status(JobReceipt("anyon", "t", "j"))["status"] == "SUCCEEDED"
    qnx = NS(jobs=NS(get=lambda **kw: "ref", status=lambda ref: NS(status="COMPLETED")))
    assert get_adapter("quantinuum", {"_client": qnx}).status(JobReceipt("quantinuum", "t", "j"))["status"] == "COMPLETED"


def test_ionq_estimate_counts_native_artifact_and_requires_explicit_usd():
    from urllib.parse import parse_qs, urlsplit
    source = {"qubits": 2, "gateset": "native", "circuit": [{"gate": "gpi", "target": 0},
        {"gate": "gpi2", "target": 1}, {"gate": "zz", "targets": [0, 1]}]}
    wire = Wire({"estimated_total_cost": 1.25, "estimated_unit": "USD"},
                {"estimated_total_cost": 125, "estimated_unit": "credits"},
                {"estimated_total_cost": float("nan"), "estimated_unit": "USD"})
    adapter = get_adapter("ionq", {"api_key": "x", "device": "qpu.forte-1", "_transport": wire})
    value = adapter.estimate(artifact("ionq", "ionq-native-json", json.dumps(source)), SubmissionOptions(mode="live", shots=17))
    assert value["usd"] == 1.25 and value["binding"] is False
    assert wire.calls[0][0] == "GET"
    assert parse_qs(urlsplit(wire.calls[0][1]).query) == {"backend": ["qpu.forte-1"], "type": ["ionq.circuit.v1"],
        "qubits": ["2"], "shots": ["17"], "1q_gates": ["2"], "2q_gates": ["1"], "error_mitigation": ["false"]}
    assert adapter.estimate(artifact("ionq", "ionq-native-json", json.dumps(source)), SubmissionOptions(mode="live"))["usd"] is None
    with pytest.raises(QStackError, match="invalid USD"):
        adapter.estimate(artifact("ionq", "ionq-native-json", json.dumps(source)), SubmissionOptions(mode="live"))


def test_braket_estimate_uses_current_regional_task_plus_shot_prices(monkeypatch):
    calls = []
    def search(**query):
        calls.append(query)
        return [{"Currency": "USD", "PricePerUnit": "0.30" if query["Product Family"] == "Quantum Task" else "0.0009"}]
    modules(monkeypatch, {"braket.tracking.pricing": NS(price_search=search)})
    adapter = get_adapter("aws", {"device": "arn:aws:braket:eu-north-1::device/qpu/iqm/Garnet"})
    result = adapter.estimate(artifact("aws"), SubmissionOptions(mode="live", shots=1000))
    assert result["usd"] == pytest.approx(1.2) and result["binding"] is False
    assert calls == [{"Region Code": "eu-north-1", "DeviceName": "Garnet", "Product Family": family}
                     for family in ("Quantum Task", "Quantum Task-Shot")]
    modules(monkeypatch, {"braket.tracking.pricing": NS(price_search=lambda **query: [])})
    assert adapter.estimate(artifact("aws"), SubmissionOptions(mode="live"))["usd"] is None
    modules(monkeypatch, {"braket.tracking.pricing": NS(price_search=lambda **query: [{"Currency": "USD", "PricePerUnit": "NaN"}])})
    with pytest.raises(QStackError, match="invalid price"):
        adapter.estimate(artifact("aws"), SubmissionOptions(mode="live"))


def test_oqc_account_contract_is_bound_to_exact_active_qpu_and_calibration(tmp_path):
    from qstack.providers.capability import calibration_digest
    calibration = {"calibration_id": "c1", "opaque_vendor_data": {"datum": 0.7}}
    client = NS(get_qpus=lambda: [{"id": "qpu1", "name": "Toshiko", "active": True}],
                get_calibration=lambda *, qpu_id: calibration)
    adapter = get_adapter("oqc", {"_client": client})
    record = adapter.discover()[0]
    assert not record["capability_verified"] and "native_gates" not in record
    assert record["calibration_sha256"] == calibration_digest(calibration)
    contract = {"schema_version": 1, "route": "oqc", "device": "qpu1", "attestation": "account-provided",
        "calibration_sha256": calibration_digest(calibration), "capability_sources": ["account hardware contract revision 1"],
        "qubits": 2, "native_gates": ["rz", "sx", "ecr", "measure"], "coupling": [[0, 1]],
        "all_to_all": False, "directed_connectivity": True, "parameter_units": "radians", "formats": ["openqasm3"],
        "supports": {"reset": False, "mid_circuit_measure": False, "feedforward": False}}
    path = tmp_path / "capabilities.json"
    path.write_text(json.dumps(contract), encoding="utf-8")
    adapter = get_adapter("oqc", {"_client": client, "device": "qpu1", "capability_snapshot": str(path)})
    verified = adapter.discover()[0]
    assert verified["capability_verified"] and verified["capability_verification"] == "account-provided-calibration-bound"
    assert verified["coupling"] == [[0, 1]]
    calibration["calibration_id"] = "c2"
    with pytest.raises(QStackError, match="calibration changed"):
        adapter.discover()
    with pytest.raises(QStackError, match="active QPUs"):
        get_adapter("oqc", {"_client": client, "device": "wrong"}).discover()
    with pytest.raises(QStackError, match="explicit device"):
        get_adapter("oqc", {"_client": client, "capability_snapshot": str(path)}).discover()


def test_oqc_unsupported_emulator_calibration_does_not_hide_hardware():
    class UnsupportedEndpoint(Exception):
        server_error_code = 405
    def calibration(*, qpu_id):
        if qpu_id == "emulator":
            raise UnsupportedEndpoint()
        return {"timestamp": "calibration-id"}
    client = NS(get_qpus=lambda: [{"id": "emulator"}, {"id": "hardware"}], get_calibration=calibration)
    records = get_adapter("oqc", {"_client": client}).discover()
    assert [row["device"] for row in records] == ["emulator", "hardware"]
    assert records[0]["calibration_sha256"] is None and records[1]["calibration_sha256"]
    assert not any(row["capability_verified"] for row in records)


@pytest.mark.parametrize("patch", [{"device": "wrong"}, {"qubits": True}, {"coupling": [[0, 9]]},
    {"parameter_units": "turns"}, {"supports": {}}, {"attestation": "inferred"}])
def test_oqc_account_contract_rejects_invalid_attestations(tmp_path, patch):
    from qstack.providers.capability import account_capability, calibration_digest
    value = {"schema_version": 1, "route": "oqc", "device": "q1", "attestation": "account-provided",
        "calibration_sha256": calibration_digest({}), "capability_sources": ["account"], "qubits": 2,
        "native_gates": ["sx"], "coupling": [], "all_to_all": False, "directed_connectivity": False,
        "parameter_units": "radians", "formats": ["openqasm3"],
        "supports": {"reset": False, "mid_circuit_measure": False, "feedforward": False}, **patch}
    path = tmp_path / "contract.json"
    path.write_text(json.dumps(value), encoding="utf-8")
    with pytest.raises(QStackError):
        account_capability(path, route="oqc", device="q1", calibration={})


def test_rigetti_calibration_uses_fidelity_not_uncertainty_or_default_durations():
    from qstack.providers.calibration import rigetti_calibrations
    def instruction(name, locus, characteristic, fidelity):
        return NS(name=name, sites=[NS(node_ids=locus, characteristics=[
            NS(name=characteristic, node_ids=locus, value=fidelity, error=0.001)])])
    isa = NS(instructions=[instruction("CZ", [2, 5], "fCZ", 0.94), instruction("MEASURE", [2], "fRO", 0.97)],
             benchmarks=[instruction("randomized_benchmark_simultaneous_1q", [5], "rb", 0.999)])
    record = rigetti_calibrations(isa)
    assert record["two_qubit_errors"][0][:2] == [2, 5]
    assert record["two_qubit_errors"][0][2] == pytest.approx(0.06)
    assert record["readout_errors"][0][1] == pytest.approx(0.03)
    assert record["one_qubit_errors"][0][1] == pytest.approx(0.001)
    assert record["instruction_durations"] == []


def test_real_qiskit_target_calibration_and_global_instruction_loci():
    pytest.importorskip("qiskit")
    from qiskit.circuit import Measure
    from qiskit.circuit.library import CXGate, RZGate, SXGate
    from qiskit.circuit import Parameter
    from qiskit.transpiler import InstructionProperties, Target
    target = Target(num_qubits=2)
    target.add_instruction(RZGate(Parameter("theta")), {None: None})
    target.add_instruction(SXGate(), {(0,): InstructionProperties(duration=32e-9, error=0.001)})
    target.add_instruction(CXGate(), {None: None})
    target.add_instruction(Measure(), {(0,): InstructionProperties(duration=1e-6, error=0.03)})
    record = native.qiskit_target_record("ibm", NS(target=target, name="test"), ["qiskit-native"])
    assert "rz" not in record["gate_loci"] and "cx" not in record["gate_loci"] and record["all_to_all"]
    assert record["one_qubit_errors"] == [[0, 0.001]] and record["readout_errors"] == [[0, 0.03]]
    assert record["instruction_durations"][0] == {"op": "sx", "qubits": [0], "duration_ns": 32.0}


def test_real_qiskit_dynamic_blocks_preserve_phase_and_physical_qubits():
    pytest.importorskip("qiskit")
    ops = [{"op": "measure", "qubits": [1], "clbits": [0]},
        {"op": "if", "clbits": [0], "condition_value": 1}, {"op": "gphase", "params": [0.25]},
        {"op": "x", "qubits": [1]}, {"op": "else"}, {"op": "reset", "qubits": [1]}, {"op": "endif"}]
    circuit = native.qiskit_circuit(artifact("ibm", ops=ops))
    branch = circuit.data[1]
    assert circuit.find_bit(branch.qubits[0]).index == 1
    assert branch.operation.condition == (circuit.clbits[0], 1)
    assert branch.operation.blocks[0].global_phase == pytest.approx(0.25)
    assert [inst.operation.name for inst in branch.operation.blocks[0].data] == ["x"]
    assert [inst.operation.name for inst in branch.operation.blocks[1].data] == ["reset"]
    assert circuit.global_phase == 0


def test_real_oqc_models_disable_tket_and_request_integer_histograms():
    sdk = pytest.importorskip("qcaas_client.client")
    pytest.importorskip("compiler_config.config")
    captured = []
    def schedule(task, *, qpu_id):
        assert isinstance(task, sdk.QPUTask)
        captured.append(task.to_json())
        return [NS(task_id="offline")]
    adapter = get_adapter("oqc", {"device": "q1", "_client": NS(schedule_tasks=schedule)})
    adapter.submit(artifact("oqc", "openqasm3", "OPENQASM 3.0;"), SubmissionOptions(mode="live", shots=7))
    value = captured[0]
    assert value["qpu_id"] == "q1" and value["program"] == "OPENQASM 3.0;"
    config = json.loads(value["config"])["$data"]
    assert config["repeats"] == 7
    assert config["optimizations"]["$data"]["tket_optimizations"]["$value"] == 1
    assert config["results_format"]["$data"]["format"]["$value"] == 1
    assert config["results_format"]["$data"]["transforms"]["$value"] == 3


def test_google_owned_schedule_groups_only_independent_operations():
    class Circuit:
        def __init__(self):
            self.moments = []
        def append(self, value, **kwargs):
            self.moments.append(value)
    cirq = NS(GridQubit=lambda r, c: (r, c), Circuit=Circuit, Moment=lambda ops: list(ops),
        rz=lambda theta: lambda q: ("rz", theta, q))
    value = artifact("google", ops=[{"op": "rz", "qubits": [0], "params": [0.1]},
        {"op": "rz", "qubits": [1], "params": [0.2]}, {"op": "rz", "qubits": [0], "params": [0.3]}])
    value.manifest["statistics"] = {"schedule": [{"instruction": 0, "layer": 0}, {"instruction": 1, "layer": 0},
        {"instruction": 2, "layer": 1}]}
    circuit = native.cirq_circuit(value, {"qubit_labels": ["0_0", "0_1"]}, module=cirq)
    assert list(map(len, circuit.moments)) == [2, 1]
    value.manifest["statistics"]["schedule"][2]["layer"] = 0
    with pytest.raises(QStackError, match="dependency"):
        native.cirq_circuit(value, {"qubit_labels": ["0_0", "0_1"]}, module=cirq)


def test_real_qcs_results_preserve_memory_and_complex_readout(monkeypatch):
    sdk = pytest.importorskip("qcs_sdk.qpu.api")
    from qcs_sdk.qpu import MemoryValues
    result = sdk.ExecutionResults(buffers={"ro0": sdk.ExecutionResult([0, 1]),
        "iq": sdk.ExecutionResult([complex(0.25, -0.5)])},
        memory={"ro": MemoryValues.Binary([1, 0])}, execution_duration_microseconds=41)
    modules(monkeypatch, {"qcs_sdk.qpu.api": NS(retrieve_results=lambda *args, **kwargs: result)})
    receipt = JobReceipt("rigetti", "Ankaa", "r1", metadata={"ro_sources": {"ro[0]": "ro0"}})
    response = get_adapter("rigetti", {"_client": "offline"}).results(receipt)
    assert response.raw["memory"]["ro"] == [1, 0]
    assert response.raw["buffers"]["ro0"]["data"] == [0, 1]
    assert response.raw["buffers"]["iq"]["data"] == [{"real": 0.25, "imag": -0.5}]
    assert response.raw["execution_duration_microseconds"] == 41
    assert response.counts is None


def test_iqm_preserves_reserved_resonator_slots_and_actual_gate_loci():
    dqa = NS(qubits=["QB1", "QB2"], computational_resonators=["CR1"], calibration_set_id=UUID(int=1),
        gates={"prx": NS(loci=[("QB1",), ("QB2",)]), "measure": NS(loci=[("QB1",), ("QB2",)]),
               "move": NS(loci=[("QB1", "CR1")]), "cz": NS(loci=[("QB2", "CR1")])})
    client = NS(quantum_computer_name="star", get_dynamic_quantum_architecture=lambda: dqa)
    adapter = get_adapter("iqm", {"_client": client})
    record = adapter.discover()[0]
    assert record["qubit_labels"] == ["QB1", "QB2", "CR1"] and record["computational_resonators"] == ["CR1"]
    assert record["computational_qubits"] == [0, 1] and record["resonator_qubits"] == [2]
    assert record["capability_verified"] and record["qubits"] == 3
    assert record["gate_loci"]["move"] == [[0, 2]] and "move" in record["native_gates"]
    assert record["coupling"] == [[0, 2], [1, 2]]
    dqa.gates["cz"].loci.append(("QB1", "QB2"))
    record = adapter.discover()[0]
    assert record["capability_verified"] and record["coupling"] == [[0, 2], [1, 2], [0, 1]]
    assert record["gate_loci"]["cz"] == [[1, 2], [0, 1]]


def test_oqc_reports_pinned_python_requirement_before_import(monkeypatch):
    monkeypatch.setattr(base.sys, "version_info", (3, 13, 0))
    with pytest.raises(QStackError, match="Python 3.12") as error:
        base.optional("qcaas_client.client", "oqc")
    assert error.value.code == "MISSING_DEPENDENCY"


def test_cloud_discovery_separates_transport_route_from_hardware_vendor(monkeypatch):
    device = NS(arn="arn:aws:braket:eu-north-1::device/qpu/iqm/Garnet", properties={"paradigm": {
        "qubitCount": 2, "nativeGateSet": ["prx", "cz"], "connectivity": {"fullyConnected": True}}})
    modules(monkeypatch, {"braket.aws": NS(AwsDevice=NS(get_devices=lambda **kwargs: [device]))})
    record = get_adapter("aws", {"_client": "offline"}).discover()[0]
    assert record["route"] == "aws" and record["vendor"] == "iqm"
    target = NS(name="ionq.qpu.forte-1", provider_id="ionq", input_data_format="ionq.circuit.v1", output_data_format="histogram")
    record = get_adapter("azure", {"_client": NS(get_targets=lambda: [target])}).discover()[0]
    assert record["route"] == "azure" and record["vendor"] == "ionq"


def test_cirq_direct_serializer_rejects_label_override_before_construction():
    value = artifact("google")
    value.target_snapshot["qubit_labels"] = ["0_0", "0_1"]
    with pytest.raises(QStackError, match="physical mapping"):
        native.cirq_circuit(value, {"qubit_labels": ["0_1", "0_0"]}, module=NS())


def test_google_counts_require_actual_joint_binary_records():
    from qstack.providers.cloud import _google_counts
    metadata = {"num_clbits": 3, "shots": 2,
        "measurement_keys": [{"key": "a", "clbit": 2}, {"key": "b", "clbit": 0}]}
    assert _google_counts({"a": [[[1]], [[0]]], "b": [[[0]], [[1]]]}, metadata) == {"100": 1, "001": 1}
    assert _google_counts({"a": [[[0.1]], [[0.9]]], "b": [[[0]], [[1]]]}, metadata) is None
    assert _google_counts({"a": [[[1]], [[0]]], "b": [[[0]]]}, metadata) is None
    assert _google_counts({"a": [[[1]], [[0]]]}, metadata) is None
    assert _google_counts({"a": [[[1]], [[0]]], "b": [[[0]], [[1]]]}, {**metadata, "shots": 3}) is None
def test_standalone_result_retains_readout_mapping_without_credential_context():
    from qstack.models import JobReceipt
    from qstack.providers.base import Adapter
    receipt = JobReceipt("test", "device", "job", artifact_hash="sha", metadata={
        "num_clbits": 6, "measurement_mapping": [{"qubit": 3, "clbit": 5}],
        "context": {"api_key": "sensitive"}})
    adapter = Adapter()
    adapter.route = "test"
    result = adapter.result(receipt, {"physical_counts": {"1": 12}}, {"1": 12}, bit_order="provider-native")
    assert result.metadata == {"num_clbits": 6, "measurement_mapping": [{"qubit": 3, "clbit": 5}],
                               "artifact_hash": "sha", "bit_order": "provider-native"}
    assert result.counts == {"1": 12}


def test_real_ibm_target_rejects_reversed_symmetric_locus_and_fixed_angle():
    pytest.importorskip("qiskit")
    from qiskit.circuit import IfElseOp
    from qiskit.circuit.library import CZGate, RXGate
    from qiskit.transpiler import Target
    target = Target(num_qubits=2)
    target.add_instruction(CZGate(), {(0, 1): None})
    target.add_instruction(RXGate(math.pi / 2), {(0,): None})
    target.add_instruction(IfElseOp, name="if_else")
    native.validate_qiskit_target({"instructions": [
        {"op": "if", "clbits": [0], "condition_value": 1},
        {"op": "cz", "qubits": [0, 1]}, {"op": "else"},
        {"op": "rx", "qubits": [0], "params": [math.pi / 2]}, {"op": "endif"}]}, target)
    for instruction in ({"op": "cz", "qubits": [1, 0]},
                        {"op": "rx", "qubits": [0], "params": [0.2]}):
        with pytest.raises(QStackError, match="ordered qubits and parameters"):
            native.validate_qiskit_target({"instructions": [instruction]}, target)
