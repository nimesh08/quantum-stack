"""jobsvc — Phase D job service.

FastAPI front for the Photon/Phonon/Spinor compiler engine. The
service does **not** rebuild compilation; it calls the C++ engine
through the Phase C nanobind binding (`photon._engine`).

Public surface (frozen at end of M2):

  - `jobsvc.main:app` — FastAPI ASGI app (alias `jobsvc.main:run` to
    start uvicorn programmatically).
  - `jobsvc.worker:run` — entrypoint for a background worker process.
"""

from __future__ import annotations

__version__ = "0.4.0+phased.m1"

__all__ = ["__version__"]
