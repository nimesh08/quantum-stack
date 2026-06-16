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
