"""Braket catalog decoding must retain actual native addresses and readiness."""
import copy
import json
from types import SimpleNamespace

import pytest

from qstack.providers import cloud
from qstack.registry import cache_targets


def capabilities():
    return {"paradigm": {"qubitCount": 3, "nativeGateSet": ["rx", "rz", "cz"],
                         "connectivity": {"fullyConnected": False,
                                          "connectivityGraph": {"0": ["2"], "2": ["0", "3"], "3": ["2"]}}},
            "action": {"braket.ir.openqasm.program": {"supportedPragmas": ["verbatim"]}}}


def device(properties, **changes):
    fields = {"arn": "arn:aws:braket:us-west-1::device/qpu/rigetti/catalog-test",
              "properties": properties, "provider_name": "Rigetti", "type": "QPU", "status": "ONLINE"}
    fields.update(changes)
    return SimpleNamespace(**fields)


class LegacyProperties:
    """Shape of the pinned Braket Pydantic-v1 public capability object."""
    def __init__(self, data):
        self.data = data

    def json(self):
        return json.dumps(self.data)


@pytest.mark.parametrize("wrap", [lambda value: value, LegacyProperties])
def test_aws_discovery_decodes_capabilities_and_keeps_sparse_physical_addresses(monkeypatch, wrap):
    raw = capabilities()
    selected = device(wrap(raw))
    session = object()
    calls = []
    def catalog(**kwargs):
        calls.append(kwargs)
        return [selected]
    monkeypatch.setattr(cloud, "optional", lambda *_: SimpleNamespace(AwsDevice=SimpleNamespace(get_devices=catalog)))
    record = cloud.AWSAdapter({"_client": session}).discover()[0]
    assert calls == [{"aws_session": session}]
    assert record["raw"] == raw
    assert record["capability_verified"] is True
    assert record["readiness"] == "discovered"
    assert record["reported_qubit_count"] == 3
    assert record["qubits"] == 4
    assert record["available_qubits"] == [0, 2, 3]
    assert record["coupling"] == [[0, 2], [2, 0], [2, 3], [3, 2]]
    assert record["execution_kind"] == "hardware"
    assert record["formats"] == ["openqasm3"]


def test_aws_discovery_accepts_real_pydantic_v1_models():
    pydantic = pytest.importorskip("pydantic.v1")
    class Properties(pydantic.BaseModel):
        paradigm: dict
        action: dict
    model = Properties(**capabilities())
    assert not hasattr(model, "model_dump")
    record = cloud._aws_device_record(device(model))
    assert record["capability_verified"] is True
    assert record["raw"] == capabilities()


def test_aws_discovery_accepts_pydantic_v2_public_model_dump():
    class Properties:
        def model_dump(self, *, mode):
            assert mode == "json"
            return capabilities()
        def json(self):
            pytest.fail("v2 should use model_dump")
    assert cloud._aws_device_record(device(Properties()))["capability_verified"] is True


def test_aws_discovery_keeps_one_based_native_addresses():
    raw = capabilities()
    raw["paradigm"].update(nativeGateSet=["prx", "cz"], connectivity={"fullyConnected": False,
        "connectivityGraph": {"1": ["2"], "2": ["1", "3"], "3": ["2"]}})
    record = cloud._aws_device_record(device(raw, provider_name="IQM"))
    assert record["native_gates"] == ["u1q", "cz"]
    assert record["qubits"] == 4 and record["available_qubits"] == [1, 2, 3]
    assert record["capability_verified"] is True


def test_aws_discovery_fully_connected_native_device_uses_declared_count():
    raw = capabilities()
    raw["paradigm"].update(nativeGateSet=["prx", "xx", "rz"], connectivity={"fullyConnected": True, "connectivityGraph": {}})
    record = cloud._aws_device_record(device(raw, provider_name="AQT"))
    assert record["native_gates"] == ["u1q", "rxx", "rz"]
    assert record["qubits"] == 3 and record["available_qubits"] == [0, 1, 2]
    assert record["all_to_all"] is True and record["capability_verified"] is True


@pytest.mark.parametrize("properties", [None, "not a capability object", {"paradigm": None},
    {"paradigm": {"qubitCount": 256}, "action": {"braket.ir.ahs.program": {}}}])
def test_aws_discovery_preserves_unsupported_catalog_entries_without_native_claims(properties, tmp_path, monkeypatch):
    record = cloud._aws_device_record(device(properties))
    assert record["capability_verified"] is False
    assert record["readiness"] == "unavailable"
    assert record["formats"] == []
    assert record["native_gates"] == []
    assert record["execution_kind"] == "hardware"
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path))
    assert cache_targets("aws", [record])[0]["capability_verified"] is False


def test_aws_discovery_simulator_is_not_native_hardware():
    record = cloud._aws_device_record(device({"paradigm": {"qubitCount": 34}, "action": {
        "braket.ir.openqasm.program": {"supportedOperations": ["h", "cnot"], "supportedPragmas": []}}},
        type="SIMULATOR", provider_name="Amazon Braket", arn="arn:aws:braket:::device/quantum-simulator/amazon/sv1"))
    assert record["execution_kind"] == "simulator"
    assert record["execution_kind_verified"] is True
    assert record["capability_verified"] is False and record["readiness"] == "unavailable"


@pytest.mark.parametrize("change", [
    {"connectivity": None},
    {"connectivity": {"fullyConnected": "true", "connectivityGraph": {}}},
    {"connectivity": {"fullyConnected": False, "connectivityGraph": {"bad": ["1"]}}},
    {"connectivity": {"fullyConnected": False, "connectivityGraph": {"0": None}}},
    {"connectivity": {"fullyConnected": False, "connectivityGraph": {"0": ["1"]}}},
    {"qubitCount": True},
])
def test_aws_discovery_inconsistent_topology_is_not_verified(change):
    raw = capabilities()
    raw["paradigm"].update(copy.deepcopy(change))
    record = cloud._aws_device_record(device(raw))
    assert record["capability_verified"] is False
    assert record["readiness"] == "needs_refresh"
    assert record["coupling"] == []


@pytest.mark.parametrize("pragmas", [None, "verbatim", [], ["other"]])
def test_aws_discovery_requires_explicit_verbatim_contract(pragmas):
    raw = capabilities()
    raw["action"]["braket.ir.openqasm.program"]["supportedPragmas"] = pragmas
    record = cloud._aws_device_record(device(raw))
    assert record["capability_verified"] is False
    assert record["readiness"] == "unavailable"


def test_aws_discovery_retired_native_devices_are_not_live_ready():
    record = cloud._aws_device_record(device(capabilities(), status="RETIRED"))
    assert record["capability_verified"] is False
    assert record["readiness"] == "retired"
