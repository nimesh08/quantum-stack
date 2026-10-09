"""photon — the user-facing Python facade for Phase C.

Imports the C++ engine via nanobind (`photon._engine`). The
`@photon.kernel` decorator lives in `photon._decorator` and uses
the engine to compile decorated functions.

Public surface (frozen at end of M4):

  - `QReg(n)`              — quantum register (helper class).
  - `kernel(func)`         — function decorator.
  - `compile_phonon(text, target='generic')` — direct engine call.
  - `__version__`          — Phase C release version.
"""

from __future__ import annotations

import sys

__version__ = "0.6.0"

if sys.version_info < (3, 12):
    raise ImportError(
        "photon requires Python 3.12+ (Phase C floor). "
        "Detected: " + sys.version
    )

# Import the C++ engine. When the extension was not built (no
# nanobind on the build host), import fails with a clear message;
# the M4 decorator path falls back to a subprocess-driven shim.
try:
    from . import _engine  # type: ignore[attr-defined]
    _ENGINE_AVAILABLE = True
except ImportError as e:  # pragma: no cover
    _engine = None  # type: ignore[assignment]
    _ENGINE_AVAILABLE = False
    _IMPORT_ERROR = e


def compile_phonon(text: str, target: str = "generic"):
    """Compatibility name for :func:`compile_logical`.

    Returns a CompiledProgram with `.ok`, `.error`, `.dump_phonon`,
    `.dump_spinor`, `.estimate()`. Raises ImportError if the C++
    binding is unavailable.
    """
    if not _ENGINE_AVAILABLE:
        raise ImportError(
            "photon._engine is unavailable: " + str(_IMPORT_ERROR)
        )
    return _engine.compile_phonon(text, target)


def compile_logical(text: str, target: str = "generic"):
    """Type-check and lower Phonon to logical Spinor.

    The returned program has stage='logical' and target_verified=False.
    Its estimates are logical proxies. Use qstack.compile_file for validated
    target-native compilation and artifacts; this function does not execute.
    """
    return compile_phonon(text, target)


# Re-export the M4 decorator as `photon.kernel` once it lands.
from ._decorator import kernel  # noqa: E402
from ._qreg import QReg  # noqa: E402


def uint(width: int, value=0):
    """Declare/cast an unsigned controller value inside a photon kernel.

    Width is a compile-time integer in [1,64]. Arithmetic wraps modulo 2**width.
    This marker is compiled from source and never executes a device value on the host.
    """
    from ._errors import PhotonKernelError
    raise PhotonKernelError("photon.uint is a source marker for @photon.kernel")


def output(value):
    """Expose a named controller value in the compiled artifact's typed outputs."""
    from ._errors import PhotonKernelError
    raise PhotonKernelError("photon.output is a source marker for @photon.kernel")


def bounded(condition, *, max_iterations: int):
    """Bound a device while loop; exhaustion is recorded per shot without filtering results."""
    from ._errors import PhotonKernelError
    raise PhotonKernelError("photon.bounded is a source marker for @photon.kernel")


def discard(register):
    """Release a quantum register; reuse resets its static pool slot before use."""
    from ._errors import PhotonKernelError
    raise PhotonKernelError("photon.discard is a source marker for @photon.kernel")

__all__ = [
    "__version__",
    "kernel",
    "QReg",
    "compile_phonon",
    "compile_logical",
    "uint",
    "output",
    "bounded",
    "discard",
]
