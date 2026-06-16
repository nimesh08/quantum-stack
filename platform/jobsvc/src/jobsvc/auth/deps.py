"""FastAPI dependencies for auth.

Two parallel paths:

  - Bearer JWT in `Authorization: Bearer <jwt>` (user-facing,
    short-lived; minted by `POST /login`).
  - API key in `X-API-Key: <plaintext>` (programmatic, long-lived).

Either is sufficient. The dependency tries JWT first, then API key.
"""

from __future__ import annotations

import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Annotated

from fastapi import Depends, Header, HTTPException, Request, status
from fastapi.security import OAuth2PasswordBearer
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from ..db import get_session
from ..models import ApiKey, User, UserRole
from .api_key import split_prefix, verify_api_key
from .jwt import JWTError, decode_token


_oauth2 = OAuth2PasswordBearer(tokenUrl="/api/v1/login", auto_error=False)


@dataclass(slots=True)
class AuthenticatedUser:
    id: uuid.UUID
    email: str
    role: UserRole
    via: str  # "jwt" | "api_key"

    @property
    def is_admin(self) -> bool:
        return self.role is UserRole.admin


async def _user_from_jwt(
    token: str | None, session: AsyncSession
) -> AuthenticatedUser | None:
    if not token:
        return None
    try:
        claims = decode_token(token)
    except JWTError:
        return None
    if claims.typ != "access":
        return None
    try:
        uid = uuid.UUID(claims.sub)
    except ValueError:
        return None
    user = (
        await session.execute(select(User).where(User.id == uid))
    ).scalar_one_or_none()
    if user is None:
        return None
    return AuthenticatedUser(
        id=user.id, email=user.email, role=user.role, via="jwt"
    )


async def _user_from_api_key(
    key: str | None, session: AsyncSession
) -> AuthenticatedUser | None:
    if not key:
        return None
    prefix = split_prefix(key)
    if prefix is None:
        return None
    rows = (
        await session.execute(
            select(ApiKey).where(
                ApiKey.prefix == prefix, ApiKey.revoked_at.is_(None)
            )
        )
    ).scalars().all()
    for row in rows:
        if verify_api_key(key, row.hash):
            row.last_used_at = datetime.now(timezone.utc)
            user = (
                await session.execute(
                    select(User).where(User.id == row.user_id)
                )
            ).scalar_one()
            return AuthenticatedUser(
                id=user.id,
                email=user.email,
                role=user.role,
                via="api_key",
            )
    return None


async def optional_user(
    request: Request,
    session: Annotated[AsyncSession, Depends(get_session)],
    bearer: Annotated[str | None, Depends(_oauth2)] = None,
    x_api_key: Annotated[str | None, Header(alias="X-API-Key")] = None,
) -> AuthenticatedUser | None:
    user = await _user_from_jwt(bearer, session)
    if user is not None:
        return user
    return await _user_from_api_key(x_api_key, session)


async def current_user(
    user: Annotated[AuthenticatedUser | None, Depends(optional_user)],
) -> AuthenticatedUser:
    if user is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="authentication required",
            headers={"WWW-Authenticate": "Bearer"},
        )
    return user


async def require_admin(
    user: Annotated[AuthenticatedUser, Depends(current_user)],
) -> AuthenticatedUser:
    if not user.is_admin:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN, detail="admin only"
        )
    return user


__all__ = [
    "AuthenticatedUser",
    "current_user",
    "optional_user",
    "require_admin",
]
