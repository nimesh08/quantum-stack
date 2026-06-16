"""HS256 JWT — access + refresh."""

from __future__ import annotations

import uuid
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from typing import Any

from jose import JWTError, jwt

from ..config import get_settings


@dataclass(slots=True)
class TokenClaims:
    sub: str
    role: str
    typ: str
    exp: int


def _now_ts() -> int:
    return int(datetime.now(timezone.utc).timestamp())


def _encode(payload: dict[str, Any]) -> str:
    s = get_settings()
    return jwt.encode(payload, s.jwt_secret, algorithm=s.jwt_algorithm)


def create_token_pair(
    user_id: uuid.UUID, role: str
) -> tuple[str, str, int]:
    """Mint a fresh access + refresh JWT pair.

    Args:
        user_id: Subject of the token (becomes the ``sub`` claim).
        role: ``"user"`` or ``"admin"`` (becomes the ``role`` claim).

    Returns:
        ``(access_token, refresh_token, expires_in_seconds)``. The
        access token is short-lived
        (``settings.jwt_access_minutes`` * 60); the refresh token
        lives for ``settings.jwt_refresh_days`` days.
    """
    s = get_settings()
    access_exp = _now_ts() + s.jwt_access_minutes * 60
    refresh_exp = _now_ts() + s.jwt_refresh_days * 24 * 3600
    access = _encode(
        {"sub": str(user_id), "role": role, "typ": "access", "exp": access_exp}
    )
    refresh = _encode(
        {"sub": str(user_id), "role": role, "typ": "refresh", "exp": refresh_exp}
    )
    return access, refresh, s.jwt_access_minutes * 60


def decode_token(token: str) -> TokenClaims:
    """Verify signature + expiry, return parsed claims.

    Args:
        token: JWT string from the ``Authorization: Bearer …`` header
            (or from ``refresh_token``).

    Returns:
        [`TokenClaims`][jobsvc.auth.jwt.TokenClaims].

    Raises:
        jose.JWTError: Signature or expiry check failed.
    """
    s = get_settings()
    payload = jwt.decode(token, s.jwt_secret, algorithms=[s.jwt_algorithm])
    return TokenClaims(
        sub=str(payload["sub"]),
        role=str(payload.get("role", "user")),
        typ=str(payload.get("typ", "access")),
        exp=int(payload.get("exp", 0)),
    )


__all__ = ["JWTError", "TokenClaims", "create_token_pair", "decode_token"]
