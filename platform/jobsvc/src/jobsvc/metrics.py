"""Prometheus metrics. Single registry; importable from anywhere."""

from __future__ import annotations

from prometheus_client import Counter, Gauge, Histogram


JOBS_TOTAL = Counter(
    "jobs_total",
    "Job count by terminal state",
    ["state"],
)
# Pre-register the standard label values so /metrics shows them at zero
# even before any job has been processed (helpful for dashboards).
for _s in ("Completed", "Rejected", "Failed"):
    JOBS_TOTAL.labels(state=_s).inc(0)
JOB_DURATION_SECONDS = Histogram(
    "job_duration_seconds",
    "End-to-end duration of a job (queue -> finish)",
    ["state"],
)
QUEUE_DEPTH = Gauge(
    "queue_depth",
    "Number of jobs in Queued state",
)
WORKER_LEASE_EXPIRATIONS_TOTAL = Counter(
    "worker_lease_expirations_total",
    "Number of jobs reclaimed after a lease expired",
)
PROVIDER_LATENCY_SECONDS = Histogram(
    "provider_latency_seconds",
    "Wall time spent inside a provider adapter",
    ["provider"],
)
ERRORS_TOTAL = Counter(
    "errors_total",
    "Errors during job execution",
    ["kind"],  # "our" | "provider"
)
CALIBRATION_REFRESH_TOTAL = Counter(
    "calibration_refresh_total",
    "Calibration fetches",
    ["chip", "ok"],
)


__all__ = [
    "CALIBRATION_REFRESH_TOTAL",
    "ERRORS_TOTAL",
    "JOBS_TOTAL",
    "JOB_DURATION_SECONDS",
    "PROVIDER_LATENCY_SECONDS",
    "QUEUE_DEPTH",
    "WORKER_LEASE_EXPIRATIONS_TOTAL",
]
