"""calibration — Heisenberg Quantum Stack nightly refresh service.

Author: Nimesh Cheedella <chnimesh0808@gmail.com>
"""

from __future__ import annotations

try:
    from importlib.metadata import version as _version
    __version__ = _version("calibration")
except Exception:  # pragma: no cover
    __version__ = "0.0.0+unknown"

__author__ = "Nimesh Cheedella"
__all__ = ["__version__", "__author__"]
