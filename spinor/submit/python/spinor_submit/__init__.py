"""Compatibility API; execution is provided by qstack's shared service."""
from __future__ import annotations
import json
import os
from pathlib import Path
from dataclasses import dataclass, field
from qstack import __version__
from qstack.config import resolve_config
from qstack.models import QStackError
from qstack.providers import ADAPTERS, UNAVAILABLE

SUPPORTED_PROVIDERS = (*ADAPTERS, *UNAVAILABLE, "local")
CASSETTE_DIR = Path(__file__).parent / "cassettes"

@dataclass
class Histogram:
    counts: dict[str, int] = field(default_factory=dict)
    shots: int = 0
    mode: str = "cassette"

@dataclass
class Job:
    id: str
    status: str
    provider: str

def _from_cassette(provider: str, program_name: str, shots: int) -> Histogram:
    if Path(program_name).name != program_name or program_name in {".", ".."}:
        raise ValueError("Invalid cassette name")
    path = CASSETTE_DIR / provider / (program_name + ".json")
    data = json.loads(path.read_text(encoding="utf-8"))
    counts = data["counts"]
    if any(type(v) is not int or v < 0 for v in counts.values()):
        raise ValueError("Invalid cassette histogram")
    # Fixture counts are evidence of a fixture, never newly sampled shots.
    return Histogram(counts, sum(counts.values()), "cassette")

def submit(qasm_text: str, chip: str, *, provider: str, shots: int = 1000,
           program_name: str = "default", mode: str | None = None,
           config: dict | None = None) -> Histogram:
    if provider not in SUPPORTED_PROVIDERS:
        raise ValueError("unknown provider: " + provider)
    if type(shots) is not int or shots <= 0:
        raise ValueError("shots must be positive")
    resolved = resolve_config(provider, config).values
    mode = mode or resolved.get("mode") or ("local" if provider == "local" else "cassette")
    if mode == "cassette":
        return _from_cassette(provider, program_name, shots)
    if provider in UNAVAILABLE:
        raise QStackError(UNAVAILABLE[provider], "UNSUPPORTED_CAPABILITY")
    if mode == "live":
        from qstack.registry import ensure_live_target, get_target
        ensure_live_target(get_target(chip, provider, resolved))
    from qstack import run_source
    resolved["provider"] = provider
    result = run_source(qasm_text, language="qasm", target=chip, mode=mode, shots=shots, config=resolved)
    if result.counts is None:
        raise QStackError("Provider returned a non-count result; use qstack.jobs results to retrieve the raw data", "NON_COUNT_RESULT")
    return Histogram(result.counts, sum(result.counts.values()), mode)
