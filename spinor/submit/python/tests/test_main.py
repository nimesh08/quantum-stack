"""Tests for the python -m spinor_submit entry point.

Hits the real submit() function in cassette mode; no network.
"""

from __future__ import annotations

import io
import os
import sys
import json
from pathlib import Path

import pytest


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT))


from spinor_submit.__main__ import main  # noqa: E402


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
def _force_cassette(monkeypatch):
    monkeypatch.setenv("SPINOR_SUBMIT_MODE", "cassette")


@pytest.fixture
def bell_qasm(tmp_path) -> Path:
    p = tmp_path / "bell.qasm3"
    p.write_text(BELL_QASM)
    return p


def test_main_submit_pretty(bell_qasm, capsys):
    rc = main([
        "submit",
        "--qasm-file", str(bell_qasm),
        "--chip", "ibm_heron_r2",
        "--provider", "ibm",
        "--shots", "1000",
        "--program-name", "bell",
    ])
    assert rc == 0
    out = capsys.readouterr().out
    assert "shots=1000" in out
    assert "histogram:" in out
    assert "|00>" in out
    assert "|11>" in out


def test_main_submit_json(bell_qasm, capsys):
    rc = main([
        "submit",
        "--qasm-file", str(bell_qasm),
        "--chip", "ibm_heron_r2",
        "--provider", "ibm",
        "--shots", "1000",
        "--program-name", "bell",
        "--json",
    ])
    assert rc == 0
    out = capsys.readouterr().out
    data = json.loads(out)
    assert data["shots"] == 1000
    assert data["total"] == 1000
    assert "00" in data["counts"]
    assert "11" in data["counts"]


def test_main_submit_unknown_provider(bell_qasm, capsys):
    # argparse should reject before we get to our handler.
    with pytest.raises(SystemExit):
        main([
            "submit",
            "--qasm-file", str(bell_qasm),
            "--chip", "ibm_heron_r2",
            "--provider", "wat",
            "--shots", "100",
            "--program-name", "bell",
        ])


def test_main_submit_each_step2_vendor_cassette(bell_qasm, capsys):
    for provider, chip in [
        ("qci", "qci_aqumen"),
        ("anyon", "anyon_yukon"),
        ("tii", "tii_falcon"),
        ("alicebob", "alicebob_boson_4"),
    ]:
        capsys.readouterr()  # drain
        rc = main([
            "submit",
            "--qasm-file", str(bell_qasm),
            "--chip", chip,
            "--provider", provider,
            "--shots", "100",
            "--program-name", "bell",
        ])
        assert rc == 0
        out = capsys.readouterr().out
        assert "histogram:" in out


def test_main_targets_lists_chips(capsys, monkeypatch):
    # Point the resolver at the in-tree registry.
    monkeypatch.setenv("SPINOR_REGISTRY_ROOT",
                       str(Path(__file__).resolve().parents[4] /
                           "spinor" / "registry"))
    rc = main(["targets"])
    assert rc == 0
    out = capsys.readouterr().out
    assert "ibm_heron_r2" in out


def test_main_providers_lists_known_ids(capsys):
    rc = main(["providers"])
    assert rc == 0
    out = capsys.readouterr().out
    for p in ("ibm", "aws", "azure", "qci", "anyon", "tii", "alicebob",
              "local"):
        assert p in out


def test_main_version(capsys):
    rc = main(["version"])
    assert rc == 0
    out = capsys.readouterr().out
    assert "spinor_submit" in out


def test_main_api_key_file_resolves(bell_qasm, tmp_path, capsys, monkeypatch):
    """Confirm --api-key-file is read and the IBM env var gets set."""
    monkeypatch.delenv("IBM_QUANTUM_TOKEN", raising=False)
    keyfile = tmp_path / "tok"
    keyfile.write_text("MY-IBM-TOKEN-123\n")
    rc = main([
        "submit",
        "--qasm-file", str(bell_qasm),
        "--chip", "ibm_heron_r2",
        "--provider", "ibm",
        "--shots", "100",
        "--program-name", "bell",
        "--api-key-file", str(keyfile),
    ])
    assert rc == 0
    assert os.environ.get("IBM_QUANTUM_TOKEN") == "MY-IBM-TOKEN-123"
