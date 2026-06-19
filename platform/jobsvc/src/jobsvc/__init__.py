"""jobsvc — Heisenberg Quantum Stack job service.

FastAPI surface for the Photon/Phonon/Spinor compiler engine. The
service does not rebuild compilation; it calls the C++ engine
through the `photon._engine` nanobind binding.

Public surface:

- ``jobsvc.main:app`` — FastAPI ASGI app.
- ``jobsvc.main:run`` — programmatic start (used by the
  ``heisenberg`` launcher).
- ``jobsvc.worker:run`` — entry point for a background worker
  process (the systemd ``heisenberg-worker.service`` calls this).

Author: Nimesh Cheedella <chnimesh0808@gmail.com>
"""

from __future__ import annotations

try:
    from importlib.metadata import version as _version
    __version__ = _version("jobsvc")
except Exception:  # pragma: no cover
    __version__ = "0.0.0+unknown"

__author__ = "Nimesh Cheedella"
__all__ = ["__version__", "__author__"]
