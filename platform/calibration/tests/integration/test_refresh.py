"""Refresh integration: run_once writes the right files; respects
`refresh: never`; provider failures keep previous file.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from calibration.main import load_plans, refresh_one, run_once


def test_load_plans_reads_yaml(registry_root) -> None:
    plans = load_plans(registry_root)
    ids = sorted(p.chip_id for p in plans)
    assert ids == ["fixturechip", "lazy"]


def test_run_once_writes_only_nightly(registry_root, tmp_path) -> None:
    results = run_once(registry_root)
    by_id = {r["chip"]: r for r in results}
    assert by_id["fixturechip"]["ok"] is True
    assert by_id["lazy"]["skipped"] is True

    fp = tmp_path / "store" / "fixturechip.json"
    assert fp.exists()
    body = json.loads(fp.read_text())
    assert body["chip_id"] == "fixturechip"
    assert (tmp_path / "store" / "lazy.json").exists() is False


def test_provider_failure_keeps_previous_file(registry_root, tmp_path,
                                              monkeypatch) -> None:
    # First refresh creates a file.
    run_once(registry_root)
    fp = tmp_path / "store" / "fixturechip.json"
    original = fp.read_bytes()

    from calibration.providers import fixture as fix
    def boom(self, _):  # noqa: ARG001
        raise RuntimeError("provider went down")
    monkeypatch.setattr(fix.FixtureProvider, "fetch", boom)

    results = run_once(registry_root)
    by_id = {r["chip"]: r for r in results}
    assert by_id["fixturechip"]["ok"] is False
    assert "provider went down" in by_id["fixturechip"]["error"]
    assert fp.read_bytes() == original  # untouched


def test_drift_reflected_in_result(registry_root, tmp_path,
                                   monkeypatch) -> None:
    run_once(registry_root)
    # Now mutate the fixture provider to return very different data.
    from calibration.providers import fixture as fix
    orig = fix.FixtureProvider.fetch
    def shifted(self, chip_id):
        body = orig(self, chip_id)
        body["single_qubit_errors"] = {
            k: v * 5.0 for k, v in body["single_qubit_errors"].items()
        }
        body["two_qubit_errors"] = {
            k: v * 5.0 for k, v in body["two_qubit_errors"].items()
        }
        return body
    monkeypatch.setattr(fix.FixtureProvider, "fetch", shifted)

    results = run_once(registry_root)
    fp_result = next(r for r in results if r["chip"] == "fixturechip")
    assert fp_result["drifted"] is True
    assert len(fp_result["drifted_qubits"]) >= 1
