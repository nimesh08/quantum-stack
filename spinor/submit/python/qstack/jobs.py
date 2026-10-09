"""Persistent, credential-free job receipts and bounded read retries."""
from __future__ import annotations

import json
import os
import time
import re
from pathlib import Path

from .config import redact_data, state_path
from .models import ExecutionResult, JobReceipt, QStackError
from .registry import canonical_json, digest


def read_with_retry(operation, *, attempts=3, initial_delay=0.25):
    """Retry transient reads only. Never pass submit/cancel/login operations here."""
    for attempt in range(attempts):
        try:
            return operation()
        except Exception as exc:
            status = getattr(exc, "status_code", None) or getattr(exc, "code", None)
            response = getattr(exc, "response", None)
            status = getattr(response, "status_code", status)
            transient = isinstance(exc, (TimeoutError, ConnectionError)) or status in {
                408, 429, 500, 502, 503, 504, "PROVIDER_CONNECTION_ERROR", "PROVIDER_TRANSIENT_ERROR"}
            if isinstance(exc, QStackError) and exc.code == "PROVIDER_HTTP_ERROR":
                transient = bool(re.search(r"HTTP (408|429|500|502|503|504)\b", str(exc)))
            if not transient or attempt + 1 == attempts:
                raise
            time.sleep(min(initial_delay * (2 ** attempt), 2.0))


def _no_secrets(value):
    return redact_data(value)


def job_path(receipt: JobReceipt) -> Path:
    return state_path() / "jobs" / (digest([receipt.route, receipt.job_id])[:32] + ".json")


def save_job(receipt: JobReceipt, result: ExecutionResult | None = None, config: dict | None = None) -> str:
    try:
        return _write_job(receipt, result, config)
    except (OSError, ValueError) as exc:
        raise QStackError(f"Job {receipt.route}/{receipt.job_id} exists, but its local receipt could not be saved. "
                          "Do not resubmit; retain this receipt and retrieve the provider job.",
                          "JOB_PERSISTENCE_FAILED", {"job_receipt": redact_data(receipt.to_dict(), config)}) from exc


def _write_job(receipt: JobReceipt, result: ExecutionResult | None = None, config: dict | None = None) -> str:
    path = job_path(receipt)
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {"receipt": receipt.to_dict()}
    if path.exists():
        previous = json.loads(path.read_bytes())
        if "result" in previous:
            data["result"] = previous["result"]
    if result:
        annotate_application_result(receipt, result)
        data["result"] = result.to_dict()
    data = redact_data(data, config)
    temporary = path.with_suffix(f".{os.getpid()}.tmp")
    temporary.write_bytes(canonical_json(data))
    temporary.replace(path)
    return str(path)


def annotate_application_result(receipt: JobReceipt, result: ExecutionResult):
    """Retain the complete histogram and raw data; never postselect or resubmit."""
    flags = receipt.metadata.get("application_outputs", [])
    if not flags:
        return
    all_bits = [bit for output in flags for bit in output.get("bits", [])]
    counts = result.counts
    observed = bool(counts) and sum(counts.values()) > 0
    correlated = observed and bool(all_bits) and all(re.fullmatch(r"[01]+", key) and len(key) > max(all_bits) for key in counts)
    if correlated:
        histograms = {}
        for output in flags:
            histogram = {}
            for key, count in counts.items():
                value = str(sum(int(key[-1-bit]) << j for j, bit in enumerate(output["bits"])))
                histogram[value] = histogram.get(value, 0) + count
            histograms[output.get("name", "loop_exhausted")] = histogram
        result.metadata["classical_counts"] = histograms
    bits = [bit for output in flags if output.get("role") == "loop_exhausted" for bit in output.get("bits", [])]
    if not bits:
        return
    if not observed or any(not re.fullmatch(r"[01]+", key) or len(key) <= max(bits) for key in counts):
        result.metadata.update(application_status="not_checked", application_status_reason="Provider response lacks the required correlated per-shot exhaustion flags")
        return
    exhausted = sum(count for key, count in counts.items() if any(key[-1-bit] == "1" for bit in bits))
    result.metadata.update(application_status="loop_exhausted" if exhausted else "completed",
                           exhausted_shots=exhausted, successful_shots=sum(counts.values())-exhausted,
                           application_outputs=flags, all_shots_preserved=True)


def load_job(reference: str) -> tuple[JobReceipt, ExecutionResult | None]:
    candidate = Path(reference)
    paths = [candidate] if candidate.is_file() else list((state_path() / "jobs").glob("*.json"))
    matches = []
    for path in paths:
        data = json.loads(path.read_bytes())
        receipt = JobReceipt.from_dict(data["receipt"])
        if candidate.is_file() or reference in {receipt.job_id, path.stem}:
            result = ExecutionResult.from_dict(data["result"]) if "result" in data else None
            matches.append((receipt, result))
    if len(matches) > 1:
        raise QStackError("Job ID is ambiguous across providers; use the saved receipt path", "AMBIGUOUS_JOB")
    if matches:
        return matches[0]
    raise QStackError("Unknown job reference; use the stored receipt path or provider job ID", "JOB_NOT_FOUND")


def wait_for_result(adapter, receipt: JobReceipt, *, timeout=600.0, poll_interval=2.0, config: dict | None = None) -> ExecutionResult:
    if timeout <= 0 or poll_interval <= 0:
        raise QStackError("timeout and poll interval must be positive")
    deadline = time.monotonic() + timeout
    while True:
        status = read_with_retry(lambda: adapter.status(receipt))
        state = str(status.get("status", status.get("state", "unknown"))).lower()
        if state in {"completed", "complete", "done", "succeeded", "success"}:
            result = read_with_retry(lambda: adapter.results(receipt))
            save_job(receipt, result, config)
            return result
        if state in {"failed", "error", "cancelled", "canceled"}:
            raise QStackError(f"Job {receipt.job_id} ended with status {state}", "JOB_FAILED")
        if time.monotonic() >= deadline:
            raise QStackError(f"Wait timed out; retrieve job {receipt.job_id} later", "WAIT_TIMEOUT")
        time.sleep(min(poll_interval, max(0, deadline - time.monotonic())))
