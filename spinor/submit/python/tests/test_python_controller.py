"""Real Python facade -> compiler -> independent verification -> C++ execution."""
from pathlib import Path

import pytest

from qstack.models import SubmissionOptions
from qstack.service import submit_artifact
from test_runtime_classical import runtime_target  # shared offline capability fixture


@pytest.fixture
def photon_api(monkeypatch):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[4] / "photon" / "frontends" / "python"))
    import photon
    monkeypatch.setattr(photon, "_engine", None)
    return photon


def checked_artifact(kernel, runtime_target, fmt="qasm3", parameters=None):
    from photon._translator import Translator
    source = Translator().translate(kernel._func, bindings=parameters or {})
    return runtime_target(source.split("\n", 1)[1], fmt)


@pytest.mark.parametrize("initial", [0, 1])
@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
def test_measurement_returns_from_different_registers_project_one_fixed_result(photon_api, runtime_target, initial, fmt):
    photon = photon_api

    @photon.kernel(target="offline-controller-fixture")
    def returned(initial):
        q = photon.QReg(1)
        other = photon.QReg(1)
        flag = photon.QReg(1)
        q.x(0)
        if initial == 1:
            flag.x(0)
        saved = flag.measure()
        if saved[0] == 1:
            return q.measure_int()
        return other.measure_int()

    artifact = checked_artifact(returned, runtime_target, fmt, {"initial": initial})
    raw = submit_artifact(artifact, SubmissionOptions(mode="local", shots=8), wait=True)
    assert sum(raw.counts.values()) == 8
    assert returned.run(shots=8, provider="quantinuum", parameters={"initial": initial}) == {str(initial): 8}


@pytest.mark.parametrize("initial", [0, 1])
def test_saved_boolean_return_survives_reset_and_last_readout(photon_api, runtime_target, initial):
    photon = photon_api

    @photon.kernel(target="offline-controller-fixture")
    def returned(initial):
        q = photon.QReg(1)
        if initial == 1:
            q.x(0)
        measured = q.measure()
        saved = measured[0]
        q.reset(0)
        measured = q.measure()
        return saved

    checked_artifact(returned, runtime_target, "qir-text", {"initial": initial})
    assert returned.run(shots=8, provider="quantinuum", parameters={"initial": initial}) == {str(initial): 8}


def test_uint64_return_wraps_exactly_without_internal_storage_leak(photon_api, runtime_target):
    photon = photon_api

    @photon.kernel(target="offline-controller-fixture")
    def returned():
        q = photon.QReg(1)
        q.x(0)
        hidden = q.measure()
        value = photon.uint(64, 18446744073709551615)
        value = value + 1
        return value

    checked_artifact(returned, runtime_target, "qir-text")
    assert returned.run(shots=8, provider="quantinuum") == {"0" * 64: 8}


def test_early_return_from_bounded_loop_is_completed_and_suppresses_fallback(photon_api, runtime_target):
    photon = photon_api

    @photon.kernel(target="offline-controller-fixture")
    def returned():
        q = photon.QReg(1)
        count = photon.uint(8, 0)
        while photon.bounded(count < 5, max_iterations=4):
            count = count + 1
            if count == 2:
                return count
        return photon.uint(8, 255)

    artifact = checked_artifact(returned, runtime_target)
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=8), wait=True)
    assert result.metadata["application_status"] == "completed"
    assert result.metadata["exhausted_shots"] == 0
    assert result.metadata["classical_counts"]["__qstack_return_value"] == {"2": 8}
    assert returned.run(shots=8, provider="quantinuum") == {"00000010": 8}


def test_exhaustion_keeps_all_shots_and_cannot_be_silently_returned_as_success(photon_api, runtime_target):
    photon = photon_api

    @photon.kernel(target="offline-controller-fixture")
    def returned():
        q = photon.QReg(1)
        count = photon.uint(8, 0)
        while photon.bounded(count < 5, max_iterations=2):
            count = count + 1
            if count == 4:
                return count
        return photon.uint(8, 255)

    artifact = checked_artifact(returned, runtime_target)
    raw = submit_artifact(artifact, SubmissionOptions(mode="local", shots=8), wait=True)
    assert raw.metadata["application_status"] == "loop_exhausted"
    assert raw.metadata["exhausted_shots"] == 8
    assert sum(raw.counts.values()) == 8
    from photon._errors import PhotonKernelError
    with pytest.raises(PhotonKernelError, match="(?i)loop.*exhaust") as failure:
        returned.run(shots=8, provider="quantinuum")
    assert sum(failure.value.result.counts.values()) == 8
    assert failure.value.result.metadata["exhausted_shots"] == 8


def test_static_loop_return_cannot_be_overwritten_by_unreachable_second_return(photon_api, runtime_target):
    photon = photon_api

    @photon.kernel(target="offline-controller-fixture")
    def returned():
        q = photon.QReg(1)
        for index in range(2):
            if index == 0:
                return photon.uint(8, 2)
                return photon.uint(8, 4)
        return photon.uint(8, 255)

    checked_artifact(returned, runtime_target)
    assert returned.run(shots=8, provider="quantinuum") == {"00000010": 8}


def test_nested_bounded_return_clears_every_active_loop_without_exhaustion(photon_api, runtime_target):
    photon = photon_api

    @photon.kernel(target="offline-controller-fixture")
    def returned():
        q = photon.QReg(1)
        outer = photon.uint(8, 0)
        inner = photon.uint(8, 0)
        while photon.bounded(outer < 2, max_iterations=2):
            while photon.bounded(inner < 3, max_iterations=3):
                inner = inner + 1
                if inner == 2:
                    return inner
            outer = outer + 1
        return photon.uint(8, 255)

    artifact = checked_artifact(returned, runtime_target, "qir-text")
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=8), wait=True)
    assert result.metadata["application_status"] == "completed"
    assert result.metadata["exhausted_shots"] == 0
    assert result.metadata["classical_counts"]["__qstack_return_value"] == {"2": 8}
    assert returned.run(shots=8, provider="quantinuum") == {"00000010": 8}
