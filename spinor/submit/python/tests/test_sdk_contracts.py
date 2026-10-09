"""Real optional SDK serialization contracts, with all network access disabled.

CI installs one pinned extra per job and sets QSTACK_TEST_SDK. That selected
SDK is mandatory; ordinary base installations may skip unavailable extras.
The tests construct provider objects but never authenticate or execute jobs.
"""
from __future__ import annotations

import importlib
import inspect
import io
import json
import os
import socket
from types import SimpleNamespace as NS

import pytest

from qstack.models import CompiledArtifact, SubmissionOptions
from qstack.providers import get_adapter
from qstack.providers.native import cirq_circuit, qiskit_circuit


def sdk(route, module):
    selected = os.environ.get("QSTACK_TEST_SDK")
    if selected and selected != route:
        pytest.skip(f"SDK matrix selected {selected}")
    if selected:
        return importlib.import_module(module)
    return pytest.importorskip(module)


@pytest.fixture(autouse=True)
def no_network(monkeypatch):
    def forbidden(*args, **kwargs):
        pytest.fail("SDK contract tests must not connect to any network service")
    monkeypatch.setattr(socket.socket, "connect", forbidden)
    monkeypatch.setattr(socket.socket, "connect_ex", forbidden)
    monkeypatch.setattr(socket, "create_connection", forbidden)


def artifact(route, ops=(), fmt="native", payload=""):
    return CompiledArtifact(route, "offline-target", fmt, payload, {
        "schema_version": 1, "name": "sdk-contract", "num_qubits": 2,
        "num_clbits": 2, "global_phase": 0.125, "instructions": list(ops),
        "measurement_mapping": [{"qubit": 0, "clbit": 1}, {"qubit": 1, "clbit": 0}],
    })


def test_ibm_real_qpy_roundtrip_preserves_native_order_phase_and_readout(monkeypatch):
    qiskit = sdk("ibm", "qiskit")
    runtime = sdk("ibm", "qiskit_ibm_runtime")
    qpy = importlib.import_module("qiskit.qpy")
    monkeypatch.setattr(qiskit, "transpile", lambda *a, **k: pytest.fail("vendor transpilation"))
    compiled = artifact("ibm", [
        {"op": "rz", "qubits": [0], "params": [0.3]},
        {"op": "sx", "qubits": [0]}, {"op": "cx", "qubits": [0, 1]},
        {"op": "measure", "qubits": [0], "clbits": [1]},
        {"op": "measure", "qubits": [1], "clbits": [0]},
    ])
    circuit = qiskit_circuit(compiled)
    buffer = io.BytesIO()
    qpy.dump(circuit, buffer)
    buffer.seek(0)
    restored = qpy.load(buffer)[0]
    assert restored == circuit
    assert restored.global_phase == pytest.approx(0.125)
    assert [entry.operation.name for entry in restored.data] == ["rz", "sx", "cx", "measure", "measure"]
    assert [restored.find_bit(entry.clbits[0]).index for entry in restored.data[-2:]] == [1, 0]
    inspect.signature(runtime.SamplerV2.run).bind(None, [restored], shots=17)


def test_google_real_engine_proto_roundtrip_preserves_native_operations():
    cirq = sdk("google", "cirq")
    sdk("google", "cirq_google")
    serializers = importlib.import_module("cirq_google.serialization.circuit_serializer")
    compiled = artifact("google", [
        {"op": "phased_xz", "qubits": [0], "params": [0.4, -0.2, 0.7]},
        {"op": "sqrt_iswap", "qubits": [0, 1]},
        {"op": "syc", "qubits": [0, 1]},
        {"op": "measure", "qubits": [1], "clbits": [0]},
    ])
    compiled.target_snapshot["qubit_labels"] = ["5_4", "5_5"]
    circuit = cirq_circuit(compiled, {})
    serializer = serializers.CircuitSerializer()
    proto = serializer.serialize(circuit)
    restored = serializer.deserialize(type(proto).FromString(proto.SerializeToString()))
    # Engine's public wire schema encodes numeric gate arguments as float32.
    # This is transport precision, not approximate compiler synthesis.
    device_proto = importlib.import_module("cirq_google.api.v2.program_pb2")
    field = device_proto.ArgValue.DESCRIPTOR.fields_by_name["float_value"]
    assert field.type == field.TYPE_FLOAT
    assert cirq.approx_eq(restored, circuit, atol=1e-7)
    assert restored.all_qubits() == {cirq.GridQubit(5, 4), cirq.GridQubit(5, 5)}
    assert cirq.measurement_key_names(restored) == {"c0"}


def test_google_repeated_classical_write_preserves_correlated_samples_and_restart(monkeypatch):
    cirq = sdk("google", "cirq")
    from qstack.models import JobReceipt
    compiled = artifact("google", [
        {"op": "measure", "qubits": [0], "clbits": [2]},
        {"op": "measure", "qubits": [1], "clbits": [0]},
        {"op": "measure", "qubits": [2], "clbits": [2]},
    ])
    compiled.physical_ir.update(num_qubits=3, num_clbits=3,
        measurement_mapping=[{"qubit": 0, "clbit": 2}, {"qubit": 1, "clbit": 0}, {"qubit": 2, "clbit": 2}])
    compiled.target_snapshot["qubit_labels"] = ["0_0", "0_1", "0_2"]
    qs = [cirq.GridQubit(0, n) for n in range(3)]
    captured = []
    processor = NS(get_device=lambda: NS(validate_circuit=lambda circuit: None),
        run_sweep=lambda circuit, **kwargs: (captured.append(circuit) or NS(job_id="offline", program_id="program")))
    adapter = get_adapter("google", {"_client": NS(get_processor=lambda target: processor), "device_config_name": "offline"})
    receipt = adapter.submit(compiled, SubmissionOptions(mode="live", shots=64))
    # A receipt reload must retain the source-ordered write mapping.
    receipt = JobReceipt(**json.loads(json.dumps(receipt.to_dict())))
    assert receipt.metadata["measurement_keys"] == [
        {"key": "c2", "clbit": 2}, {"key": "c0", "clbit": 0}, {"key": "c2__1", "clbit": 2}]
    prepared = cirq.Circuit(cirq.H(qs[0]), cirq.CNOT(qs[0], qs[1]), cirq.X(qs[2]))
    result = cirq.Simulator(seed=712).run(prepared + captured[0], repetitions=64)
    monkeypatch.setattr(adapter, "job", lambda receipt: NS(results=lambda: [result]))
    output = adapter.results(receipt)
    assert set(output.counts) == {"100", "101"}
    assert sum(output.counts.values()) == 64
    assert output.raw[0]["records"]["c2"] == output.raw[0]["records"]["c0"]
    assert all(shot == [[1]] for shot in output.raw[0]["records"]["c2__1"])
    assert output.metadata["bit_order"] == "classical-msb-first"


def test_google_previous_receipts_with_repeated_cirq_keys_remain_retrievable(monkeypatch):
    cirq = sdk("google", "cirq")
    from qstack.models import JobReceipt
    qs = [cirq.GridQubit(0, n) for n in range(3)]
    circuit = cirq.Circuit(cirq.H(qs[0]), cirq.CNOT(qs[0], qs[1]), cirq.X(qs[2]),
        cirq.measure(qs[0], key="c2"), cirq.measure(qs[1], key="c0"), cirq.measure(qs[2], key="c2"))
    result = cirq.Simulator(seed=712).run(circuit, repetitions=64)
    with pytest.raises(ValueError, match="repeated keys"):
        _ = result.measurements
    receipt = JobReceipt(route="google", target="offline", job_id="offline", metadata={"num_clbits": 3,
        "measurement_mapping": [{"qubit": 0, "clbit": 2}, {"qubit": 1, "clbit": 0}, {"qubit": 2, "clbit": 2}]})
    adapter = get_adapter("google", {})
    monkeypatch.setattr(adapter, "job", lambda receipt: NS(results=lambda: [result]))
    output = adapter.results(receipt)
    assert set(output.counts) == {"100", "101"}
    assert sum(output.counts.values()) == 64
    assert output.raw[0]["measurements"] is None
    assert len(output.raw[0]["records"]["c2"][0]) == 2


def test_aws_real_openqasm_program_preserves_physical_verbatim_payload():
    ir = sdk("aws", "braket.ir.openqasm")
    aws = sdk("aws", "braket.aws")
    source = "OPENQASM 3.0;\nbit[2] c;\n#pragma braket verbatim\nbox { rx(0.25) $0; iswap $0,$1; }\nc[0] = measure $1;\n"
    program = ir.Program(source=source)
    encoded = json.loads(program.json())
    assert encoded["source"] == source
    assert encoded["braketSchemaHeader"]["name"] == "braket.ir.openqasm.program"
    assert ir.Program.parse_raw(program.json()).source == source
    inspect.signature(aws.AwsDevice.run).bind(None, program, shots=17, disable_qubit_rewiring=True)


def test_aws_real_iqm_capability_model_keeps_controller_extensions_out_of_native_basis():
    from qstack.providers.cloud import _aws_device_record
    schema = sdk("aws", "braket.device_schema.iqm")
    aws = sdk("aws", "braket.aws")
    advertised = ["cz", "prx", "cc_prx", "measure_ff", "barrier"]
    # A minimal valid real SDK model; the advertised operations match the
    # current IQM Braket contract, including experimental feedback extensions.
    properties = schema.IqmDeviceCapabilities(
        service={"executionWindows": [], "shotsRange": [1, 20000]}, deviceParameters={},
        action={"braket.ir.openqasm.program": {"actionType": "braket.ir.openqasm.program",
            "version": ["1"], "supportedOperations": advertised, "supportedPragmas": ["verbatim"],
            "supportPhysicalQubits": True, "disabledQubitRewiringSupported": True}},
        paradigm={"qubitCount": 2, "nativeGateSet": advertised,
            "connectivity": {"fullyConnected": False, "connectivityGraph": {"1": ["2"], "2": ["1"]}}})
    record = _aws_device_record(NS(properties=properties, type=aws.AwsDeviceType.QPU,
        provider_name="IQM", status="ONLINE", arn="arn:aws:braket:eu-north-1::device/qpu/iqm/model-contract"))
    assert record["native_gates"] == ["cz", "u1q", "barrier"]
    assert record["unsupported_native_operations"] == ["cc_prx", "measure_ff"]
    assert record["advertised_native_operations"] == advertised
    assert record["raw"] == json.loads(properties.json())
    assert record["available_qubits"] == [1, 2] and record["qubits"] == 3
    assert record["capability_verified"] is True and record["execution_kind"] == "hardware"
    assert record["supports"] == {"reset": False, "mid_circuit_measure": False, "feedforward": False}


@pytest.mark.parametrize("provider", ["ionq", "quantinuum"])
def test_azure_real_target_encodes_payload_and_explicit_shots(provider, monkeypatch):
    target_sdk = sdk("azure", "qdk.azure.target")
    captured = []
    workspace = NS(append_user_agent=lambda value: None)
    target = (target_sdk.IonQ(workspace, name="ionq.qpu.forte-1") if provider == "ionq"
              else target_sdk.Quantinuum(workspace, name="quantinuum.qpu.h2-1"))
    def from_input_data(**kwargs):
        captured.append(kwargs)
        return NS(id="offline")
    # Intercept only the final transport boundary. Encoding, target defaults,
    # shot conversion and provider parameter handling all use the real SDK.
    monkeypatch.setattr(type(target), "_get_job_class", staticmethod(lambda: NS(from_input_data=from_input_data)))
    workspace.get_targets = lambda **kwargs: target
    if provider == "ionq":
        data = {"qubits": 2, "gateset": "native", "circuit": [{"gate": "zz", "targets": [0, 1], "angle": 0.125}]}
        compiled = artifact("azure", fmt="ionq-native-json", payload=json.dumps(data))
    else:
        compiled = artifact("azure", fmt="qir-bitcode", payload=b"BC\xc0\xde\x00owned-payload")
    adapter = get_adapter("azure", {"_client": workspace, "device": target.name})
    receipt = adapter.submit(compiled, SubmissionOptions(mode="live", shots=17))
    assert receipt.job_id == "offline"
    request = captured[0]
    assert request["input_params"]["shots"] == 17
    assert "num_shots" not in request
    if provider == "ionq":
        assert json.loads(request["input_data"]) == data
        assert request["input_params"]["error-mitigation"] == {"debias": False}
    else:
        assert request["input_data"] == compiled.payload
        assert request["input_data_format"] == "qir.v1"
        assert request["input_params"]["entryPoint"] == "main"
        assert request["input_params"]["no-opt"] is True
        assert request["input_params"]["noreduce"] is True


def test_quantinuum_real_backend_models_preserve_owned_compile_options():
    qnx = sdk("quantinuum", "qnexus")
    pyqir = sdk("quantinuum", "pyqir")
    config = qnx.models.QuantinuumConfig(device_name="H2-1", no_opt=True,
        allow_implicit_swaps=False, allow_2q_gate_rebase=False, simplify_initial=False)
    wire = config.model_dump(mode="json")
    assert wire["no_opt"] is True
    for key in ("allow_implicit_swaps", "allow_2q_gate_rebase", "simplify_initial"):
        assert wire[key] is False
    assert type(config).model_validate(wire) == config
    helios = qnx.models.HeliosConfig(system_name="Helios-1")
    assert helios.model_dump(mode="json")["system_name"] == "Helios-1"
    inspect.signature(qnx.start_execute_job).bind(programs=[], n_shots=[], backend_config=config,
                                                 name="offline", project=None)
    module = pyqir.Module.from_ir(pyqir.Context(), "define void @main() { ret void }")
    assert module.verify() is None
    assert pyqir.Module.from_bitcode(pyqir.Context(), module.bitcode).verify() is None
