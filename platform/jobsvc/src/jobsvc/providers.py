"""Provider routing.

Maps `chip.provider` (ibm/aws/azure/local) to the Phase A submission
adapter `spinor_submit.submit(qasm_text, chip, provider=..., shots=...)`.

When `spinor_submit` is not installed the local path falls back to a
deterministic in-process simulator so the platform's worker tests can
still cover the full state-machine flow.

`error_kind` is one of:

  - "our"      — our code raised (compile, lookup, etc.)
  - "provider" — the provider adapter raised
"""

from __future__ import annotations

import math
import random
import time
from dataclasses import dataclass
from typing import Any

from .engine import EngineResult
from .metrics import ERRORS_TOTAL, PROVIDER_LATENCY_SECONDS
from .registry import ChipInfo


@dataclass(slots=True)
class SubmissionOutcome:
    ok: bool
    counts: dict[str, int]
    shots: int
    raw_payload: dict[str, Any] | None = None
    error: str = ""
    error_kind: str = ""


def submit_to_provider(
    *, engine: EngineResult, chip: ChipInfo, shots: int,
    program_name: str = "default",
) -> SubmissionOutcome:
    provider = chip.provider
    qasm = engine.spinor_qasm3 or _fallback_qasm(engine, chip)
    t0 = time.monotonic()
    try:
        if provider == "local":
            counts = _local_simulate(engine, chip, shots)
            outcome = SubmissionOutcome(
                ok=True, counts=counts, shots=shots,
                raw_payload={"provider": "local"},
            )
        else:
            counts, raw = _via_spinor_submit(
                qasm, chip, provider=provider, shots=shots,
                program_name=program_name,
            )
            outcome = SubmissionOutcome(
                ok=True, counts=counts, shots=shots,
                raw_payload=raw,
            )
        return outcome
    except _OurError as exc:
        ERRORS_TOTAL.labels(kind="our").inc()
        return SubmissionOutcome(
            ok=False, counts={}, shots=shots,
            error=str(exc), error_kind="our",
        )
    except Exception as exc:  # noqa: BLE001
        ERRORS_TOTAL.labels(kind="provider").inc()
        return SubmissionOutcome(
            ok=False, counts={}, shots=shots,
            error=str(exc), error_kind="provider",
        )
    finally:
        PROVIDER_LATENCY_SECONDS.labels(provider=provider).observe(
            time.monotonic() - t0
        )


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


class _OurError(Exception):
    """Raised when failure is unambiguously ours (e.g. unknown chip)."""


def _via_spinor_submit(
    qasm: str, chip: ChipInfo, *, provider: str, shots: int,
    program_name: str,
) -> tuple[dict[str, int], dict[str, Any]]:
    try:
        import spinor_submit  # type: ignore
    except ImportError as exc:
        raise _OurError(
            "spinor_submit is not installed; "
            "cannot route to provider "
            f"{provider!r}. Install Phase A: "
            "pip install -e quantum-stack/spinor/submit/python"
        ) from exc

    histogram = spinor_submit.submit(
        qasm, chip.id, provider=provider, shots=shots,
        program_name=program_name,
    )
    counts = {str(k): int(v) for k, v in histogram.counts.items()}
    return counts, {
        "provider": provider, "shots": histogram.shots,
        "via": "spinor_submit",
    }


def _fallback_qasm(engine: EngineResult, chip: ChipInfo) -> str:
    n = max(engine.estimate.num_qubits, 1)
    return f"OPENQASM 3.0;\n// fallback emit\nqubit[{n}] q;\n"


def _local_simulate(
    engine: EngineResult, chip: ChipInfo, shots: int
) -> dict[str, int]:
    """Tiny in-process simulator: returns a uniform Bell-like
    distribution for any program with at least one two-qubit gate
    on at least 2 qubits, otherwise a single-bitstring deterministic
    outcome. NOT a real simulator; only here so the worker has
    something to do when `local` is selected without spinor_submit.
    """
    n = max(engine.estimate.num_qubits, 1)
    rng = random.Random(0)
    if engine.estimate.two_qubit_count >= 1 and n >= 2:
        # Bell-like: 50/50 between |00...0> and |11...1>.
        zero = "0" * n
        one = "1" * n
        zeros = sum(1 for _ in range(shots) if rng.random() < 0.5)
        return {zero: zeros, one: shots - zeros}
    # Default: all zeros.
    return {"0" * n: shots}


__all__ = ["SubmissionOutcome", "submit_to_provider"]
