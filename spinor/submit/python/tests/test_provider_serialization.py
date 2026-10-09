"""Offline serialization rejects unverified executable extensions before loading."""
import sys
import types

import pytest

from qstack.models import CompiledArtifact, QStackError
from qstack.providers import validate_serialization


@pytest.mark.parametrize("config", [
    {"qibolab_bridge": "qstack_test_unsafe_factory:create"},
    {"qibolab_bridge": "qstack_test_unsafe_factory:create", "qibolab_platform": "dummy"},
    {"_bridge": {"native": True}, "qibolab_platform": "dummy"},
])
def test_dry_run_never_calls_custom_laboratory_factory(config, monkeypatch):
    calls = []
    module = types.ModuleType("qstack_test_unsafe_factory")
    module.create = lambda config: calls.append(config)
    monkeypatch.setitem(sys.modules, module.__name__, module)
    artifact = CompiledArtifact("qibolab", "dummy", "qibolab-native", "", {})
    with pytest.raises(QStackError, match="custom bridge"):
        validate_serialization(artifact, config)
    assert calls == []


def test_dry_run_requires_explicit_calibrated_platform():
    artifact = CompiledArtifact("qibolab", "dummy", "qibolab-native", "", {})
    with pytest.raises(QStackError, match="requires qibolab_platform"):
        validate_serialization(artifact)
