"""Persistent, credential-free job receipts and bounded read retries."""
from __future__ import annotations

import json
import os
import time
import re
from pathlib import Path

from .config import SECRET_FIELDS, state_path
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
    if isinstance(value, dict):
        return {key: _no_secrets(item) for key, item in value.items() if key.lower() not in SECRET_FIELDS
                and key.lower() not in {"authorization", "credentials", "password", "headers"}}
    if isinstance(value, (list, tuple)):
        return [_no_secrets(item) for item in value]
    return value


def job_path(receipt: JobReceipt) -> Path:
    return state_path() / "jobs" / (digest([receipt.route, receipt.job_id])[:32] + ".json")


def save_job(receipt: JobReceipt, result: ExecutionResult | None = None) -> str:
    try:
        return _write_job(receipt, result)
    except (OSError, ValueError) as exc:
        raise QStackError(f"Job {receipt.route}/{receipt.job_id} exists, but its local receipt could not be saved. "
                          "Do not resubmit; retain this receipt and retrieve the provider job.",
                          "JOB_PERSISTENCE_FAILED", {"job_receipt": _no_secrets(receipt.to_dict())}) from exc


def _write_job(receipt: JobReceipt, result: ExecutionResult | None = None) -> str:
    path = job_path(receipt)
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {"receipt": _no_secrets(receipt.to_dict())}
    if path.exists():
        previous = json.loads(path.read_bytes())
        if "result" in previous:
            data["result"] = previous["result"]
    if result:
        data["result"] = _no_secrets(result.to_dict())
    temporary = path.with_suffix(f".{os.getpid()}.tmp")
    temporary.write_bytes(canonical_json(data))
    temporary.replace(path)
    return str(path)


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


def wait_for_result(adapter, receipt: JobReceipt, *, timeout=600.0, poll_interval=2.0) -> ExecutionResult:
    if timeout <= 0 or poll_interval <= 0:
        raise QStackError("timeout and poll interval must be positive")
    deadline = time.monotonic() + timeout
    while True:
        status = read_with_retry(lambda: adapter.status(receipt))
        state = str(status.get("status", status.get("state", "unknown"))).lower()
        if state in {"completed", "complete", "done", "succeeded", "success"}:
            result = read_with_retry(lambda: adapter.results(receipt))
            save_job(receipt, result)
            return result
        if state in {"failed", "error", "cancelled", "canceled"}:
            raise QStackError(f"Job {receipt.job_id} ended with status {state}", "JOB_FAILED")
        if time.monotonic() >= deadline:
            raise QStackError(f"Wait timed out; retrieve job {receipt.job_id} later", "WAIT_TIMEOUT")
        time.sleep(min(poll_interval, max(0, deadline - time.monotonic())))
