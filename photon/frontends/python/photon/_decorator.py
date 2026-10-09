"""@photon.kernel decorator (M4)."""

from __future__ import annotations

from typing import Any, Callable, Optional
import inspect

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
        self._signature = inspect.signature(func)
        self.phonon_text = "" if self._signature.parameters else translate(func, target=target)
        self.compiled = None if self._signature.parameters else self._compile_now()

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
        compiled = _engine.compile_phonon(self.phonon_text, self.target)
        if not compiled.ok:
            raise CompilationError(compiled.error)
        return compiled

    def __call__(self, *args, **kwargs) -> Any:
        """Run a single local shot using bound kernel parameters."""
        bound = self._signature.bind(*args, **kwargs)
        bound.apply_defaults()
        return self.run(shots=1, parameters=bound.arguments)

    def run(self, shots: int = 1024,
            target: Optional[str] = None, *, mode: str = "local",
            parameters: Optional[dict[str, Any]] = None, **options) -> dict[str, int]:
        """Execute through qstack and return its measured counts.

        Local simulation is the default. Select ``mode='live'`` and pass
        provider/configuration options explicitly for hardware execution.
        Numeric kernel parameters are bound before translation/compilation.
        """
        if isinstance(shots, bool) or not isinstance(shots, int) or shots <= 0:
            raise ValueError("shots must be a positive integer")
        try:
            bound = self._signature.bind(**(parameters or {}))
        except TypeError as error:
            raise CompilationError(f"kernel parameters: {error}") from error
        bound.apply_defaults()
        selected_target = target or self.target
        source = translate(self._func, target=selected_target, bindings=bound.arguments)
        try:
            import qstack
        except ImportError as error:
            raise PhotonKernelError("execution requires the qstack runtime package") from error
        result = qstack.run_source(source, language="phonon", target=selected_target,
                                  mode=mode, shots=shots, **options)
        counts = result.counts
        if counts is None:
            raise PhotonKernelError("execution completed without measurement counts")
        return dict(counts)


def kernel(func: Optional[Callable[..., Any]] = None,
           *, target: str = "generic"):
    """`@photon.kernel` (or `@photon.kernel(target=…)`)."""
    if func is None:
        def _wrap(f):
            return _PhotonKernel(f, target=target)
        return _wrap
    return _PhotonKernel(func, target=target)
