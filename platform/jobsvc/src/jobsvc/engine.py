"""Thin wrapper around the Phase C nanobind binding.

Three responsibilities:

1.  Translate ``(source, source_kind, target)`` into a call into the
    engine and return an [`EngineResult`][jobsvc.engine.EngineResult].
2.  Provide a graceful fallback when the binding isn't available on
    the host (CI without LLVM, dev machine without ``nanobind``). In
    fallback mode every call returns a deterministic stub estimate so
    the rest of the platform can be tested end-to-end without the
    engine. This is the same pattern Phase C uses
    (``_ENGINE_AVAILABLE`` flag); see the photon facade.
3.  Surface compile errors as structured Python data; never raise
    from a successful but-rejected program.

Real engine path:

```pycon
>>> from jobsvc.engine import compile_program
>>> from jobsvc.models import SourceKind
>>> r = compile_program(
...     "target generic\\nqubit q[2]\\nh q[0]\\ncx q[0], q[1]\\n",
...     SourceKind.spinor, "generic")
>>> r.ok, r.estimate.num_qubits, r.estimate.two_qubit_count
(True, 2, 1)
```

Fallback path: same shape, deterministic stub estimate.
"""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass
from typing import Any

from .models import SourceKind


@dataclass(slots=True)
class ResourceEstimate:
    """Cost-relevant counts from the compiled program.

    The values are read directly off the Phonon -> Spinor lowering and
    fed to [`jobsvc.cost.check_budget`][jobsvc.cost.check_budget] before
    a job is queued.

    Attributes:
        num_qubits: Number of qubits the program allocates.
        depth: Linear-depth proxy: count of non-allocation ops.
        two_qubit_count: Number of two-qubit gates (cx / cz / swap /
            ecr / ms / rzz). The dominant error source.
        t_count: Number of T / Tdg gates. Magic-state count for
            fault-tolerant resource accounting.
    """

    num_qubits: int = 0
    depth: int = 0
    two_qubit_count: int = 0
    t_count: int = 0

    def to_dict(self) -> dict[str, int]:
        """Serialise to a JSON-safe ``dict[str, int]``.

        Returns:
            Mapping with keys ``num_qubits``, ``depth``, ``two_qubit_count``,
            ``t_count``. Used as the persisted shape on
            [`Job.estimate`][jobsvc.models.Job.estimate].

        Example:
            >>> ResourceEstimate(num_qubits=2, two_qubit_count=1).to_dict()
            {'num_qubits': 2, 'depth': 0, 'two_qubit_count': 1, 't_count': 0}
        """
        return {
            "num_qubits": self.num_qubits,
            "depth": self.depth,
            "two_qubit_count": self.two_qubit_count,
            "t_count": self.t_count,
        }


@dataclass(slots=True)
class EngineResult:
    """Outcome of a single ``compile_program`` invocation.

    On success, ``ok`` is True and ``error`` is empty; on failure the
    inverse. The two paths share a uniform shape so the caller can
    branch on ``ok`` without try/except.

    Attributes:
        ok: True if compilation succeeded.
        error: Diagnostic text from the engine; empty when ``ok``.
        target: Chip id used for compilation (``"generic"`` if blank).
        estimate: Resource estimate (zero-valued when not ``ok``).
        spinor_qasm3: Verbatim OpenQASM 3 the worker submits to
            providers. Empty when ``ok`` is False.
        raw_phonon: Optional dump of the Phonon IR (debug aid).
        raw_spinor: Optional dump of the Spinor IR (debug aid).
    """

    ok: bool
    error: str
    target: str
    estimate: ResourceEstimate
    spinor_qasm3: str
    raw_phonon: str = ""
    raw_spinor: str = ""

    def to_summary(self) -> dict[str, Any]:
        """Compact view of the result, suitable for log messages.

        Returns:
            ``{"ok", "target", "estimate"}`` only — drops the source
            dumps and the QASM3 payload to keep log volume low.

        Example:
            >>> e = EngineResult(True, "", "generic",
            ...                  ResourceEstimate(num_qubits=2), "")
            >>> e.to_summary()["target"]
            'generic'
        """
        return {
            "ok": self.ok,
            "target": self.target,
            "estimate": self.estimate.to_dict(),
        }


# ---------------------------------------------------------------------------
# Try to import the real engine.
# ---------------------------------------------------------------------------

_ENGINE = None
_ENGINE_ERROR: str | None = None
try:  # pragma: no cover (engine availability depends on host)
    import photon  # type: ignore

    if getattr(photon, "_ENGINE_AVAILABLE", False):
        _ENGINE = photon._engine  # type: ignore[attr-defined]
    else:
        _ENGINE_ERROR = "photon._engine is not available on this host"
except ImportError as exc:  # pragma: no cover
    _ENGINE_ERROR = f"photon import failed: {exc}"


def engine_available() -> bool:
    """Whether the C++ engine is importable on this host.

    When False, [`compile_program`][jobsvc.engine.compile_program]
    silently switches to a deterministic stub. The platform's tests
    never need the real engine — the stub returns a shape-compatible
    estimate so the cost seam, queue, and worker can all be exercised
    end-to-end without LLVM.

    Returns:
        True if ``photon._engine`` was imported successfully.

    Example:
        >>> isinstance(engine_available(), bool)
        True
    """
    return _ENGINE is not None


# ---------------------------------------------------------------------------
# Compile entry points
# ---------------------------------------------------------------------------


def compile_program(
    source: str, kind: SourceKind, target: str
) -> EngineResult:
    """Compile any of the three source forms.

    For Photon and Spinor sources we wrap into a Phonon-text shim
    if the full ``photon::lang`` binding is not exposed yet (the
    current Phase C binding only exports ``compile_phonon``; D6
    documents the next binding addition). For now we accept Phonon
    directly and treat Spinor as a trivial Phonon body.

    Args:
        source: Program text. Length 1 to 200 000 characters
            (enforced by [`schemas.JobCreate`][jobsvc.schemas.JobCreate]).
        kind: One of [`SourceKind.photon`][jobsvc.models.SourceKind.photon],
            [`SourceKind.phonon`][jobsvc.models.SourceKind.phonon], or
            [`SourceKind.spinor`][jobsvc.models.SourceKind.spinor].
        target: Chip id from the Phase A registry, or the empty
            string (treated as ``"generic"``). Unknown chips are
            **not** validated here — the API layer rejects them with
            HTTP 400 before this function is called.

    Returns:
        [`EngineResult`][jobsvc.engine.EngineResult]. Inspect ``ok``
        first; on False, ``error`` carries the diagnostic.

    Raises:
        ValueError: ``kind`` is not a recognised
            [`SourceKind`][jobsvc.models.SourceKind] value.

    Example:
        >>> from jobsvc.models import SourceKind
        >>> bell = ("target generic\\nqubit q[2]\\n"
        ...         "h q[0]\\ncx q[0], q[1]\\n")
        >>> r = compile_program(bell, SourceKind.spinor, "generic")
        >>> r.ok and r.estimate.num_qubits == 2
        True
        >>> r.estimate.two_qubit_count
        1
    """
    if not target:
        target = "generic"

    if _ENGINE is None:
        return _stub_compile(source, kind, target)

    phonon_text = _to_phonon_text(source, kind)

    cp = _ENGINE.compile_phonon(phonon_text, target)
    if not cp.ok:
        return EngineResult(
            ok=False,
            error=cp.error,
            target=target,
            estimate=ResourceEstimate(),
            spinor_qasm3="",
        )

    est = cp.estimate()
    qasm3 = ""
    if hasattr(_ENGINE, "emit_qasm3_verbatim"):  # binding D3
        qasm3 = _ENGINE.emit_qasm3_verbatim(cp)
    return EngineResult(
        ok=True,
        error="",
        target=target,
        estimate=ResourceEstimate(
            num_qubits=int(est.num_qubits),
            depth=int(est.depth),
            two_qubit_count=int(est.two_qubit_count),
            t_count=int(est.t_count),
        ),
        spinor_qasm3=qasm3 or _qasm3_shim(cp.dump_spinor()),
        raw_phonon=cp.dump_phonon(),
        raw_spinor=cp.dump_spinor(),
    )


# ---------------------------------------------------------------------------
# Source normalisation helpers (private)
# ---------------------------------------------------------------------------


_PHONON_HEAD_RE = re.compile(r"^\s*target\s+\w+", re.MULTILINE)


def _to_phonon_text(source: str, kind: SourceKind) -> str:
    """Coerce `source` of any kind into Phonon text the engine accepts.

    Phonon is a superset of Spinor (every Spinor program is valid
    Phonon); Photon lowering is exposed by the C++ engine through a
    later binding addition (D6) and meanwhile we surface a clear
    error.
    """
    if kind is SourceKind.phonon or kind is SourceKind.spinor:
        if not _PHONON_HEAD_RE.search(source):
            return f"target generic\n{source}"
        return source
    if kind is SourceKind.photon:
        return source
    raise ValueError(f"unknown source kind {kind!r}")


# ---------------------------------------------------------------------------
# Stub fallback (deterministic)
# ---------------------------------------------------------------------------


_GATE_RE = re.compile(r"^\s*([a-z]+)\b", re.MULTILINE)
_QUBIT_DECL_RE = re.compile(r"^\s*qubit\s+\w+\s*\[\s*(\d+)\s*\]", re.MULTILINE)
_TWO_QUBIT_GATES = {"cx", "cz", "swap", "ecr", "ms", "rzz"}
_T_GATES = {"t", "tdg"}


def _stub_compile(
    source: str, kind: SourceKind, target: str
) -> EngineResult:
    """Heuristic estimate so the platform can be tested without the
    engine. NOT a compiler; the analysis is intentionally trivial.
    """
    if not source.strip():
        return EngineResult(
            ok=False,
            error="empty program",
            target=target,
            estimate=ResourceEstimate(),
            spinor_qasm3="",
        )

    nq = 0
    for m in _QUBIT_DECL_RE.finditer(source):
        nq = max(nq, int(m.group(1)))

    two_qubit = 0
    t_count = 0
    depth = 0
    for line in source.splitlines():
        m = _GATE_RE.match(line)
        if not m:
            continue
        name = m.group(1)
        if name in {"target", "qubit", "bit"}:
            continue
        depth += 1
        if name in _TWO_QUBIT_GATES:
            two_qubit += 1
        if name in _T_GATES:
            t_count += 1

    if nq == 0:
        nq = 1

    digest = hashlib.sha256(source.encode()).hexdigest()[:12]
    qasm3 = (
        "OPENQASM 3.0;\n"
        f"// stub-compile {digest} target={target} kind={kind.value}\n"
        f"qubit[{nq}] q;\n"
        f"bit[{nq}] c;\n"
        + "\n".join(
            f"// {line}" for line in source.splitlines()[:20] if line.strip()
        )
        + "\n"
    )
    return EngineResult(
        ok=True,
        error="",
        target=target,
        estimate=ResourceEstimate(
            num_qubits=nq,
            depth=depth,
            two_qubit_count=two_qubit,
            t_count=t_count,
        ),
        spinor_qasm3=qasm3,
        raw_phonon=source,
        raw_spinor=source,
    )


def _qasm3_shim(spinor_text: str) -> str:
    """When the binding doesn't expose `emit_qasm3_verbatim` (older
    Phase C build), wrap the spinor dump as a comment-laden QASM3.
    The cassette-mode submission adapters do not actually parse this;
    the live adapters do (and live mode is gated behind env)."""
    return (
        "OPENQASM 3.0;\n"
        + "// fallback emit; replace via emit_qasm3_verbatim binding\n"
        + "\n".join(f"// {line}" for line in spinor_text.splitlines())
        + "\n"
    )


__all__ = [
    "EngineResult",
    "ResourceEstimate",
    "compile_program",
    "engine_available",
]
