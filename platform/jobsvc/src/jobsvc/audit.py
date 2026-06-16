"""Helpers that emit `AuditLog` rows in the same session/transaction as
the change they record. Importable from anywhere; never opens a new
session.
"""

from __future__ import annotations

import uuid
from typing import Any

from sqlalchemy.ext.asyncio import AsyncSession

from .models import AuditLog


async def record(
    session: AsyncSession,
    *,
    user_id: uuid.UUID | None,
    action: str,
    target_type: str | None = None,
    target_id: str | uuid.UUID | None = None,
    detail: dict[str, Any] | None = None,
    ip: str | None = None,
    ua: str | None = None,
) -> AuditLog:
    """Append an audit-log entry inside the caller's transaction.

    Never opens a new session; never commits — your transaction
    decides when (and whether) to flush. By keeping audit writes in
    the same transaction as the change they record, we get
    "audit-or-it-didn't-happen" semantics for free.

    Args:
        session: Async SQLAlchemy session in your enclosing
            transaction.
        user_id: User performing the action, or ``None`` for system
            events.
        action: Stable action code (e.g. ``"job.created"``,
            ``"login"``, ``"api_key.revoked"``).
        target_type: Type of the affected entity (e.g. ``"job"``,
            ``"user"``, ``"api_key"``). Optional.
        target_id: Id of the affected entity. Coerced to ``str``.
        detail: Optional structured payload (becomes JSONB in
            Postgres).
        ip: Caller's IP (set from request when available).
        ua: Caller's User-Agent (set from request when available).

    Returns:
        The persisted [`AuditLog`][jobsvc.models.AuditLog] row.
    """
    log = AuditLog(
        user_id=user_id,
        action=action,
        target_type=target_type,
        target_id=str(target_id) if target_id is not None else None,
        detail=detail,
        ip=ip,
        ua=ua,
    )
    session.add(log)
    await session.flush()
    return log


__all__ = ["record"]
