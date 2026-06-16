"""jobsvc seed CLI.

Usage:

    JOBSVC_DATABASE_URL=postgresql+asyncpg://...:5432/jobsvc \\
        python -m jobsvc.seed admin@local password admin

Idempotent: if the user already exists, prints the existing row and
exits 0 without changing anything.
"""

from __future__ import annotations

import argparse
import asyncio
import sys
from datetime import datetime, timezone

from sqlalchemy import select

from .auth.passwords import hash_password
from .db import session_scope
from .models import Budget, User, UserRole


async def _seed(email: str, password: str, role: UserRole) -> None:
    async with session_scope() as session:
        existing = (
            await session.execute(select(User).where(User.email == email))
        ).scalar_one_or_none()
        if existing is not None:
            print(f"user already exists: {existing.id} ({existing.role.value})")
            return
        u = User(email=email, password_hash=hash_password(password), role=role)
        session.add(u)
        await session.flush()
        session.add(Budget(user_id=u.id))
        await session.commit()
        print(f"created: {u.id} ({u.email}, {u.role.value})")


def main() -> None:  # pragma: no cover (CLI)
    p = argparse.ArgumentParser(prog="jobsvc.seed")
    p.add_argument("email")
    p.add_argument("password")
    p.add_argument("role", nargs="?", default="user", choices=["user", "admin"])
    args = p.parse_args()
    asyncio.run(_seed(args.email, args.password, UserRole(args.role)))


if __name__ == "__main__":  # pragma: no cover
    main()
