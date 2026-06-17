"""M10 — Submission adapter tests.

Cassette-mode tests run offline. Live tests are skipped unless
SPINOR_SUBMIT_MODE=live and the relevant SDKs are installed.
"""

from __future__ import annotations

import os
import sys

import pytest


HERE = os.path.dirname(__file__)
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from spinor_submit import (  # noqa: E402  (sys.path manipulation)
    Histogram,
    SUPPORTED_PROVIDERS,
    submit,
)


BELL_QASM = """OPENQASM 3.0;
include "stdgates.inc";
qubit[2] q;
bit[2] c;
h q[0];
cx q[0], q[1];
c[0] = measure q[0];
c[1] = measure q[1];
"""


@pytest.fixture(autouse=True)
def _force_cassette_mode(monkeypatch):
    monkeypatch.setenv("SPINOR_SUBMIT_MODE", "cassette")


@pytest.mark.parametrize("provider, chip", [
    ("ibm", "ibm_heron_r2"),
    ("aws", "ionq_forte"),
    ("azure", "quantinuum_helios"),
])
def test_cassette_returns_bell_histogram(provider, chip):
    hist = submit(BELL_QASM, chip=chip, provider=provider,
                  shots=1000, program_name="bell")
    assert isinstance(hist, Histogram)
    assert hist.shots == 1000
    # Bell histogram: only 00 and 11 present, roughly 50/50.
    assert "00" in hist.counts
    assert "11" in hist.counts
    total = sum(hist.counts.values())
    assert total == 1000
    bias = abs(hist.counts["00"] - hist.counts["11"]) / total
    assert bias < 0.10  # within 10 percentage points


@pytest.mark.parametrize("provider, chip", [
    ("qci", "qci_aqumen"),
    ("anyon", "anyon_yukon"),
    ("tii", "tii_falcon"),
    ("alicebob", "alicebob_boson_4"),
])
def test_step2_cassette_returns_bell_histogram(provider, chip):
    """Step-2 vendors ship cassette-only; verify the cassette path."""
    hist = submit(BELL_QASM, chip=chip, provider=provider,
                  shots=1000, program_name="bell")
    assert isinstance(hist, Histogram)
    assert hist.shots == 1000
    assert "00" in hist.counts
    assert "11" in hist.counts
    total = sum(hist.counts.values())
    assert total == 1000


@pytest.mark.parametrize("provider, chip", [
    ("qci", "qci_aqumen"),
    ("anyon", "anyon_yukon"),
    ("tii", "tii_falcon"),
    ("alicebob", "alicebob_boson_4"),
])
def test_step2_live_mode_raises(provider, chip, monkeypatch):
    """Live mode for Step-2 vendors must raise a clear RuntimeError
    pointing the user at chips_unsupported.md, never silently fall
    through to a fabricated endpoint.
    """
    monkeypatch.setenv("SPINOR_SUBMIT_MODE", "live")
    with pytest.raises(RuntimeError, match="not wired"):
        submit(BELL_QASM, chip=chip, provider=provider,
               shots=10, program_name="bell")


def test_unknown_provider_raises():
    with pytest.raises(ValueError):
        submit(BELL_QASM, chip="ibm_heron_r2",
               provider="not_real", shots=10)


def test_supported_providers_list():
    assert "ibm" in SUPPORTED_PROVIDERS
    assert "aws" in SUPPORTED_PROVIDERS
    assert "azure" in SUPPORTED_PROVIDERS
    assert "local" in SUPPORTED_PROVIDERS
    # Step-2 vendors.
    assert "qci" in SUPPORTED_PROVIDERS
    assert "anyon" in SUPPORTED_PROVIDERS
    assert "tii" in SUPPORTED_PROVIDERS
    assert "alicebob" in SUPPORTED_PROVIDERS


def test_cassette_missing_raises(monkeypatch, tmp_path):
    monkeypatch.setenv("SPINOR_SUBMIT_MODE", "cassette")
    with pytest.raises(FileNotFoundError):
        submit(BELL_QASM, chip="ibm_heron_r2", provider="ibm",
               shots=10, program_name="not_recorded")


@pytest.mark.skipif(
    os.environ.get("SPINOR_LIVE_IBM_TEST") != "1",
    reason="live IBM tests require SPINOR_LIVE_IBM_TEST=1 + tokens",
)
def test_live_ibm_smoke():
    os.environ["SPINOR_SUBMIT_MODE"] = "live"
    hist = submit(BELL_QASM, chip="ibm_heron_r2",
                  provider="ibm", shots=200, program_name="bell")
    assert hist.shots > 0
