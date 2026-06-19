"""Heisenberg Quantum Stack — single-command launcher.

`heisenberg run` brings up jobsvc + worker + scheduler against a
SQLite database and serves the playground SPA. See ``heisenberg --help``.

Author: Nimesh Cheedella <chnimesh0808@gmail.com>
"""

from __future__ import annotations

try:
    from importlib.metadata import version as _version
    __version__ = _version("heisenberg")
except Exception:  # pragma: no cover
    __version__ = "0.0.0+unknown"

__author__ = "Nimesh Cheedella"
__all__ = ["__version__", "__author__"]
