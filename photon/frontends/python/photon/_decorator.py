"""@photon.kernel decorator (M4)."""

from __future__ import annotations

from typing import Any, Callable, Optional

from ._errors import CompilationError, PhotonKernelError
from ._translator import translate


class _PhotonKernel:
    """A function decorated with @photon.kernel.

    Attributes
    ----------
    phonon_text : str
        The Phonon source produced from the function body.
    compiled : Optional[CompiledProgram]
        The engine's compiled-program handle, or None when the C++
        engine was unavailable at decorate time (the kernel still
        carries the Phonon text so it can be passed elsewhere).
    target : str
        The target id requested at decorate time (default "generic").
    """

    def __init__(self, func: Callable[..., Any], target: str = "generic"):
        self._func = func
        self.__name__ = getattr(func, "__name__", "<kernel>")
        self.__doc__ = func.__doc__
        self.target = target
        self.phonon_text = translate(func, target=target)
        self.compiled = self._compile_now()

    def _compile_now(self):
        try:
            from . import _engine  # type: ignore[attr-defined]
        except ImportError:
            return None
        # `_engine` may be None when the package shipped without the
        # nanobind extension (build-host had no nanobind, or the
        # binary wheel is in skeleton mode). The Photon facade exposes
        # this as a `None` attribute rather than a missing import, so
        # we handle both cases here.
        if _engine is None or not hasattr(_engine, "compile_phonon"):
            return None
        return _engine.compile_phonon(self.phonon_text, self.target)

    def __call__(self, *args, **kwargs) -> Any:
        # Direct execution falls back to the Python source body. The
        # full path (engine -> simulator / submit) lands at M6.
        return self._func(*args, **kwargs)

    def run(self, shots: int = 1024,
            target: Optional[str] = None) -> dict:
        """Simulator/submit entry. M4 returns a stubbed histogram so
        the API shape is testable; M6 wires real execution.
        """
        # Simple deterministic stub: every shot yields '0'*N.
        n = 0
        # Probe phonon_text for `qubit q[N]` declaration.
        for line in self.phonon_text.splitlines():
            line = line.strip()
            if line.startswith("qubit "):
                # `qubit q[N]`
                lb, rb = line.find("["), line.find("]")
                if lb > 0 and rb > lb:
                    n = int(line[lb + 1:rb])
                    break
        outcome = "0" * max(n, 1)
        return {outcome: shots}


def kernel(func: Optional[Callable[..., Any]] = None,
           *, target: str = "generic"):
    """`@photon.kernel` (or `@photon.kernel(target=…)`)."""
    if func is None:
        def _wrap(f):
            return _PhotonKernel(f, target=target)
        return _wrap
    return _PhotonKernel(func, target=target)
