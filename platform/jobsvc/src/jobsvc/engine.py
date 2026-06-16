"""Thin wrapper around the Phase C nanobind binding.

Three responsibilities:

1. Translate `(source, source_kind, target)` into a call into the
   engine and return a `EngineResult`.
2. Provide a graceful fallback when the binding isn't available on
   the host (CI without LLVM, dev machine without `nanobind`). In
   fallback mode every call returns a deterministic stub estimate so
   the rest of the platform can be tested end-to-end without the
   engine. This is the same pattern Phase C uses
   (`_ENGINE_AVAILABLE` flag); see [`photon/__init__.py`].
3. Surface compile errors as structured Python data; never raise
   from a successful but-rejected program.

Real engine path:

    >>> r = compile_program("target generic\\nqubit q[2]\\nh q[0]\\n",
    ...                     SourceKind.spinor, "generic")
    >>> r.ok and r.estimate.num_qubits == 2

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
    num_qubits: int = 0
    depth: int = 0
    two_qubit_count: int = 0
    t_count: int = 0

    def to_dict(self) -> dict[str, int]:
        return {
            "num_qubits": self.num_qubits,
            "depth": self.depth,
            "two_qubit_count": self.two_qubit_count,
            "t_count": self.t_count,
        }


@dataclass(slots=True)
class EngineResult:
    ok: bool
    error: str
    target: str
    estimate: ResourceEstimate
    spinor_qasm3: str  # verbatim QASM3 the worker submits to providers
    raw_phonon: str = ""
    raw_spinor: str = ""

    def to_summary(self) -> dict[str, Any]:
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
    return _ENGINE is not None


# ---------------------------------------------------------------------------
# Compile entry points
# ---------------------------------------------------------------------------


def compile_program(
    source: str, kind: SourceKind, target: str
) -> EngineResult:
    """Compile any of the three source forms.

    For Photon and Spinor we wrap into a Phonon-text shim if the
    full photon::lang binding is not exposed yet. (The current Phase C
    binding only exports `compile_phonon`; D6 documents the next
    binding addition.) For now we accept Phonon directly and treat
    Spinor as a trivial Phonon body.
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
# Source normalisation helpers
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
        # When the photon-lower binding lands we'll call it here.
        # Until then, accept and pass through; the C++ engine's
        # parser will produce a structured error for the user.
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
