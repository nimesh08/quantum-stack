"""AWS Braket calibration — stub at v1 (D5).

When live mode is needed: implement using `from braket.aws import AwsDevice;
device.properties` per AwsDeviceProperties. For now the provider raises
so the writer keeps the previous file.
"""

from __future__ import annotations

from typing import Any

from .base import CalibrationProvider


class AwsProvider(CalibrationProvider):
    name = "aws"

    def fetch(self, chip_id: str) -> dict[str, Any]:
        raise NotImplementedError(
            "AWS Braket calibration provider is a v1 stub (D5). "
            "Implement using braket.aws.AwsDevice.properties."
        )


__all__ = ["AwsProvider"]
