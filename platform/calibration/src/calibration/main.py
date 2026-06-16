"""Calibration scheduler entry point.

Single-replica APScheduler that runs nightly and fetches calibration
data per chip. Idempotent so a second instance is safe but
unnecessary.

Run from a shell:

    calibration --once    # run all chips once and exit (cron-like)
    calibration           # background scheduler
"""

from __future__ import annotations

import argparse
import logging
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import structlog
import yaml
from apscheduler.schedulers.blocking import BlockingScheduler
from apscheduler.triggers.cron import CronTrigger

from . import __version__
from .diff import diff
from .providers import get_provider
from .writer import read_existing, write_atomic


_log = structlog.get_logger("calibration")


@dataclass(slots=True)
class ChipPlan:
    chip_id: str
    provider_name: str
    refresh_kind: str  # "nightly" | "never" | ...
    store_path: Path


def load_plans(registry_root: Path) -> list[ChipPlan]:
    """Read every chip YAML under ``<registry_root>/chips`` into a plan.

    Args:
        registry_root: Path to ``spinor/registry/`` containing a
            ``chips/`` subdirectory of YAML files.

    Returns:
        Sorted list of [`ChipPlan`][calibration.main.ChipPlan]
        objects, one per YAML.

    Example:
        >>> # plans = load_plans(Path("spinor/registry"))
        >>> # [p.chip_id for p in plans]
        True
        True
    """
    plans: list[ChipPlan] = []
    chips_dir = registry_root / "chips"
    for p in sorted(chips_dir.glob("*.yaml")):
        with p.open() as f:
            doc = yaml.safe_load(f) or {}
        cal = doc.get("calibration") or {}
        store = cal.get("store") or ""
        store_path = Path(store).expanduser() if store else Path.home() / (
            f".cache/spinor/calibration/{doc['id']}.json"
        )
        plans.append(
            ChipPlan(
                chip_id=str(doc["id"]),
                provider_name=str(cal.get("source") or "fixture"),
                refresh_kind=str(cal.get("refresh") or "never"),
                store_path=store_path,
            )
        )
    return plans


def _resolve_provider(name: str) -> str:
    """Map the chip YAML `calibration.source` value (e.g.
    'ibm_runtime_api', 'fixture') onto the provider registry."""
    if name in ("ibm_runtime_api", "ibm"):
        return "ibm"
    if name == "aws":
        return "aws"
    if name == "azure":
        return "azure"
    return "fixture"


def refresh_one(
    plan: ChipPlan,
    *,
    db_writer=None,  # callable(SnapshotRow) for `calibration_snapshots`
    metrics=None,
) -> dict[str, Any]:
    """Run one chip's refresh end-to-end.

    1.  Skip if ``plan.refresh_kind != "nightly"``.
    2.  Look up the provider name and fetch.
    3.  On success: atomically write the JSON, compute drift vs the
        previous file, optionally record a ``calibration_snapshots``
        row (``ok=True``).
    4.  On fetch failure: previous file is left intact, snapshot row
        records ``ok=False``, ``error`` carries the message.

    Args:
        plan: Chip's plan as parsed by
            [`load_plans`][calibration.main.load_plans].
        db_writer: Optional callable accepting a snapshot dict — wired
            from a daemon that needs to record refresh history. The
            single-process ``--once`` CLI passes ``None``.
        metrics: Optional Prometheus ``Counter`` (``calibration_refresh_total``
            with labels ``chip``/``ok``).

    Returns:
        Per-chip result dict with ``chip``, ``ok``, and on success
        ``sha`` + ``drifted`` + the drift detail. On skip,
        ``skipped: True``.

    Example:
        >>> # results = run_once(Path("spinor/registry"))
        >>> # results[0]["ok"]
        True
        True
    """
    log = _log.bind(chip=plan.chip_id, provider=plan.provider_name)
    if plan.refresh_kind != "nightly":
        log.info("refresh.skipped", reason="not nightly")
        return {"chip": plan.chip_id, "ok": True, "skipped": True}

    provider_key = _resolve_provider(plan.provider_name)
    try:
        provider = get_provider(provider_key)
        body = provider.fetch(plan.chip_id)
    except Exception as exc:  # noqa: BLE001
        log.error("refresh.failed", error=str(exc))
        if metrics is not None:
            metrics.labels(chip=plan.chip_id, ok="false").inc()
        if db_writer is not None:
            db_writer({
                "chip_id": plan.chip_id, "ok": False,
                "error": str(exc), "source_provider": provider_key,
                "fetched_at": datetime.now(timezone.utc),
            })
        return {"chip": plan.chip_id, "ok": False, "error": str(exc)}

    old = read_existing(plan.store_path)
    digest = write_atomic(plan.store_path, body)
    drift = diff(old, body)
    log.info("refresh.ok", path=str(plan.store_path), sha=digest[:12],
             drift=drift.has_drift)
    if metrics is not None:
        metrics.labels(chip=plan.chip_id, ok="true").inc()
    if db_writer is not None:
        db_writer({
            "chip_id": plan.chip_id,
            "ok": True,
            "source_provider": provider_key,
            "body": body,
            "sha256": digest,
            "written_path": str(plan.store_path),
            "fetched_at": datetime.now(timezone.utc),
        })
    return {
        "chip": plan.chip_id, "ok": True,
        "sha": digest, "drifted": drift.has_drift,
        "drifted_qubits": drift.drifted_qubits,
        "drifted_pairs": drift.drifted_pairs,
    }


def run_once(registry_root: Path) -> list[dict[str, Any]]:
    """Run [`refresh_one`][calibration.main.refresh_one] across every chip.

    Used by the ``--once`` CLI mode for cron-like invocation, and by
    the scheduler's nightly trigger.

    Args:
        registry_root: ``spinor/registry/`` path.

    Returns:
        List of per-chip result dicts, in YAML-sort order.
    """
    plans = load_plans(registry_root)
    return [refresh_one(p) for p in plans]


def _setup_logging() -> None:
    logging.basicConfig(level=logging.INFO, stream=sys.stdout, format="%(message)s")
    structlog.configure(
        processors=[
            structlog.processors.TimeStamper(fmt="iso"),
            structlog.processors.JSONRenderer(),
        ]
    )


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(prog="calibration")
    p.add_argument("--once", action="store_true", help="run once and exit")
    p.add_argument(
        "--registry",
        default=Path(__file__).resolve().parents[3] / "spinor" / "registry",
        type=Path,
    )
    p.add_argument("--cron-hour", default=2, type=int)
    p.add_argument("--cron-minute", default=0, type=int)
    return p.parse_args()


def run() -> None:  # pragma: no cover (long-running)
    _setup_logging()
    args = parse_args()
    if args.once:
        results = run_once(args.registry)
        for r in results:
            print(r)
        return
    scheduler = BlockingScheduler(timezone="UTC")
    scheduler.add_job(
        run_once, CronTrigger(hour=args.cron_hour, minute=args.cron_minute),
        args=[args.registry], id="nightly_refresh",
    )
    _log.info("scheduler.start", version=__version__,
              hour=args.cron_hour, minute=args.cron_minute)
    scheduler.start()


__all__ = ["ChipPlan", "load_plans", "refresh_one", "run", "run_once"]
