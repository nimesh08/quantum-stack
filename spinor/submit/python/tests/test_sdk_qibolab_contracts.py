"""Pinned Qibolab native pulse objects; no instruments or cloud are connected."""
import importlib
import importlib.metadata
import math
import os
import socket

import pytest

from qstack.models import CompiledArtifact, QStackError, SubmissionOptions
from qstack.providers import get_adapter, validate_serialization
from qstack.providers.qibolab_native import acquisition_counts, build_native_sequences, load_platform, platform_snapshot


@pytest.fixture
def sdk(monkeypatch):
    selected = os.environ.get("QSTACK_TEST_SDK")
    if selected and selected != "qibolab":
        pytest.skip(f"SDK matrix selected {selected}")
    def forbidden(*args, **kwargs):
        pytest.fail("Qibolab contract tests must not connect to hardware/network")
    monkeypatch.setattr(socket.socket, "connect", forbidden)
    module = importlib.import_module("qibolab") if selected else pytest.importorskip("qibolab")
    assert importlib.metadata.version("qibolab") == "0.2.17"
    return module


def program(platform):
    operations = [{"op": "u1q", "qubits": [0], "params": [math.pi / 2, 0.25]},
        {"op": "u1q", "qubits": [1], "params": [math.pi / 2, 0.5]},
        {"op": "rz", "qubits": [0], "params": [0.3]},
        {"op": "cz", "qubits": [0, 2]},
        {"op": "measure", "qubits": [0], "clbits": [2]},
        {"op": "measure", "qubits": [2], "clbits": [0]}]
    ir = {"num_qubits": 5, "num_clbits": 3, "instructions": operations,
        "measurement_mapping": [{"qubit": 0, "clbit": 2}, {"qubit": 2, "clbit": 0}]}
    manifest = {"statistics": {"schedule": [{"instruction": i, "layer": max(0, i - 1)} for i in range(len(operations))]}}
    return CompiledArtifact("qibolab", platform.name, "qibolab-native", "", ir, platform_snapshot(platform), manifest)


def test_builtin_assembler_uses_calibrated_native_pulses_and_owns_readout_mapping(sdk, monkeypatch):
    platform = sdk.create_platform("dummy")
    artifact = program(platform)
    assert artifact.target_snapshot["capability_verified"] is False
    assert artifact.target_snapshot["readiness"] == "offline_only"
    sequences, acquisitions = build_native_sequences(platform, artifact)
    assert len(sequences) == 1 and isinstance(sequences[0], sdk.PulseSequence)
    # Two owned layer-zero rotations share their start time on distinct channels.
    assert next(iter(sequences[0].channel(platform.qubits[0].drive))).kind == "pulse"
    assert next(iter(sequences[0].channel(platform.qubits[1].drive))).kind == "pulse"
    rotations = [pulse for _, pulse in sequences[0] if isinstance(pulse, sdk.VirtualZ)]
    assert any(pulse.phase == pytest.approx(-0.3) for pulse in rotations)
    assert [item["clbit"] for item in acquisitions] == [2, 0]
    assert len({item["id"] for item in acquisitions}) == 2
    offline = validate_serialization(artifact, {"_platform": platform})
    assert offline["serializer"] == "qibolab-calibrated-native" and offline["acquisitions"] == 2
    calls = []
    monkeypatch.setattr(platform, "connect", lambda: calls.append("connect"))
    monkeypatch.setattr(platform, "disconnect", lambda: calls.append("disconnect"))
    def execute(sequences, **kwargs):
        calls.append(kwargs)
        ids = [pulse.id for _, pulse in sequences[0].acquisitions]
        return {ids[0]: [1, 0, 1], ids[1]: [0, 1, 1]}
    monkeypatch.setattr(platform, "execute", execute)
    adapter = get_adapter("qibolab", {"_platform": platform})
    receipt = adapter.submit(artifact, SubmissionOptions(mode="live", shots=3))
    assert calls == ["connect", {"nshots": 3, "acquisition_type": sdk.AcquisitionType.DISCRIMINATION,
        "averaging_mode": sdk.AveragingMode.SINGLESHOT}, "disconnect"]
    assert adapter.results(receipt).counts == {"100": 1, "001": 1, "101": 1}


def test_standard_platform_directory_loads_without_global_environment_change(sdk, tmp_path, monkeypatch):
    directory = tmp_path / "configured-platform"
    directory.mkdir()
    (directory / "platform.py").write_text("from qibolab import create_platform\ndef create():\n    return create_platform('dummy')\n")
    monkeypatch.delenv("QIBOLAB_PLATFORMS", raising=False)
    platform = load_platform(str(directory))
    assert platform.name == "dummy" and "QIBOLAB_PLATFORMS" not in os.environ
    artifact = program(platform)
    artifact.target_snapshot["calibration_sha256"] = "stale"
    with pytest.raises(QStackError, match="calibration"):
        build_native_sequences(platform, artifact)


def test_acquisition_counts_preserve_zero_bits_and_do_not_round_probabilities():
    mapping = [{"id": "a", "clbit": 0}, {"id": "b", "clbit": 2}]
    assert acquisition_counts({"a": [[1], [0]], "b": [[0], [1]]}, mapping, 3, 2) == {"001": 1, "100": 1}
    assert acquisition_counts({"a": [0.1, 0.9], "b": [0, 1]}, mapping, 3, 2) is None
