"""Provider unit tests."""

from __future__ import annotations

import pytest

from calibration.providers import (
    AwsProvider,
    AzureProvider,
    FixtureProvider,
    get_provider,
)


def test_fixture_provider_returns_payload() -> None:
    p = FixtureProvider()
    body = p.fetch("ibm_heron_r2")
    assert body["chip_id"] == "ibm_heron_r2"
    assert "single_qubit_errors" in body
    assert "two_qubit_errors" in body
    assert "readout_errors" in body


def test_aws_provider_stub_raises() -> None:
    with pytest.raises(NotImplementedError):
        AwsProvider().fetch("any")


def test_azure_provider_stub_raises() -> None:
    with pytest.raises(NotImplementedError):
        AzureProvider().fetch("any")


def test_get_provider_unknown() -> None:
    with pytest.raises(KeyError):
        get_provider("nope")


def test_get_provider_known() -> None:
    p = get_provider("fixture")
    assert isinstance(p, FixtureProvider)
