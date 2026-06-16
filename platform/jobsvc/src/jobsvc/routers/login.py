"""POST /login — email + password → JWT pair."""

from __future__ import annotations

from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from .. import audit
from ..auth import create_token_pair, verify_password
from ..db import get_session
from ..models import User
from ..schemas import LoginRequest, TokenPair


router = APIRouter(prefix="/api/v1", tags=["auth"])


@router.post(
    "/login",
    response_model=TokenPair,
    responses={401: {"description": "invalid credentials"}},
)
async def login(
    body: LoginRequest,
    session: Annotated[AsyncSession, Depends(get_session)],
) -> TokenPair:
    user = (
        await session.execute(select(User).where(User.email == body.email))
    ).scalar_one_or_none()
    if user is None or not verify_password(body.password, user.password_hash):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="invalid credentials",
        )
    access, refresh, expires = create_token_pair(user.id, user.role.value)
    await audit.record(
        session, user_id=user.id, action="login", target_type="user",
        target_id=user.id,
    )
    return TokenPair(
        access_token=access,
        refresh_token=refresh,
        expires_in=expires,
    )
