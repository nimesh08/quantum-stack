"""APScheduler trigger schedules at the right cadence."""

from __future__ import annotations

from datetime import datetime, timedelta, timezone

from apscheduler.triggers.cron import CronTrigger


def test_cron_fires_at_02_utc() -> None:
    t = CronTrigger(hour=2, minute=0, timezone=timezone.utc)
    base = datetime(2026, 1, 1, 0, 30, tzinfo=timezone.utc)
    nxt = t.get_next_fire_time(None, base)
    assert nxt is not None
    assert nxt.hour == 2 and nxt.minute == 0
    assert nxt > base
