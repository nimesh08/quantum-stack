"""Provider registry."""

from __future__ import annotations

from .base import CalibrationProvider
from .aws import AwsProvider
from .azure import AzureProvider
from .fixture import FixtureProvider
from .ibm import IbmProvider


_REGISTRY: dict[str, type[CalibrationProvider]] = {
    "fixture": FixtureProvider,
    "ibm": IbmProvider,
    "aws": AwsProvider,
    "azure": AzureProvider,
}


def get_provider(name: str) -> CalibrationProvider:
    cls = _REGISTRY.get(name)
    if cls is None:
        raise KeyError(f"unknown calibration provider: {name}")
    return cls()


__all__ = [
    "AwsProvider",
    "AzureProvider",
    "CalibrationProvider",
    "FixtureProvider",
    "IbmProvider",
    "get_provider",
]
