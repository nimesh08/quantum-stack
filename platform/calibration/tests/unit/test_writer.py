"""Atomic writer + drift unit tests."""

from __future__ import annotations

from pathlib import Path

import pytest

from calibration.diff import diff
from calibration.writer import read_existing, write_atomic


def test_write_atomic_creates_dirs_and_file(tmp_path: Path) -> None:
    p = tmp_path / "deep" / "nested" / "cal.json"
    digest = write_atomic(p, {"hello": "world"})
    assert p.exists()
    assert read_existing(p) == {"hello": "world"}
    assert len(digest) == 64


def test_write_atomic_overwrites_idempotently(tmp_path: Path) -> None:
    p = tmp_path / "cal.json"
    d1 = write_atomic(p, {"a": 1})
    d2 = write_atomic(p, {"a": 1})
    assert d1 == d2  # same payload -> same hash


def test_write_atomic_changes_with_content(tmp_path: Path) -> None:
    p = tmp_path / "cal.json"
    d1 = write_atomic(p, {"a": 1})
    d2 = write_atomic(p, {"a": 2})
    assert d1 != d2


def test_write_atomic_no_temp_left(tmp_path: Path) -> None:
    p = tmp_path / "cal.json"
    write_atomic(p, {"a": 1})
    leftovers = [x.name for x in tmp_path.iterdir() if x.name != "cal.json"]
    assert leftovers == []


def test_read_existing_returns_none_for_missing(tmp_path: Path) -> None:
    assert read_existing(tmp_path / "nope.json") is None


def test_diff_no_old_means_no_drift() -> None:
    new = {"single_qubit_errors": {"0": 0.001}}
    r = diff(None, new)
    assert not r.has_drift


def test_diff_within_threshold_no_drift() -> None:
    old = {"single_qubit_errors": {"0": 0.001}, "two_qubit_errors": {}}
    new = {"single_qubit_errors": {"0": 0.0011}, "two_qubit_errors": {}}
    r = diff(old, new, relative_threshold=0.5)
    assert not r.has_drift


def test_diff_over_threshold_flags() -> None:
    old = {
        "single_qubit_errors": {"0": 0.001, "1": 0.001},
        "two_qubit_errors": {"0-1": 0.01},
    }
    new = {
        "single_qubit_errors": {"0": 0.001, "1": 0.005},  # +400%
        "two_qubit_errors": {"0-1": 0.02},                # +100%
    }
    r = diff(old, new, relative_threshold=0.5)
    assert r.has_drift
    assert r.drifted_qubits == ["1"]
    assert r.drifted_pairs == ["0-1"]


def test_diff_zero_old_skips() -> None:
    old = {"single_qubit_errors": {"0": 0.0}}
    new = {"single_qubit_errors": {"0": 0.5}}
    r = diff(old, new)
    assert not r.has_drift
