"""/me + /me/budget + /me/api-keys + admin user routes."""

from __future__ import annotations

import uuid
from datetime import datetime, timezone
from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from .. import audit
from ..auth import (
    AuthenticatedUser,
    current_user,
    hash_password,
    new_api_key,
    require_admin,
)
from ..db import get_session
from ..models import ApiKey, Budget, User
from ..schemas import (
    ApiKeyCreated,
    ApiKeyOut,
    BudgetOut,
    BudgetPatch,
    CreateApiKey,
    CreateUser,
    UserOut,
)


router = APIRouter(prefix="/api/v1", tags=["users"])


@router.get("/me", response_model=UserOut)
async def me(
    user: Annotated[AuthenticatedUser, Depends(current_user)],
    session: Annotated[AsyncSession, Depends(get_session)],
) -> UserOut:
    u = (
        await session.execute(select(User).where(User.id == user.id))
    ).scalar_one()
    return UserOut.model_validate(u)


@router.get("/me/budget", response_model=BudgetOut)
async def get_my_budget(
    user: Annotated[AuthenticatedUser, Depends(current_user)],
    session: Annotated[AsyncSession, Depends(get_session)],
) -> BudgetOut:
    b = (
        await session.execute(
            select(Budget).where(Budget.user_id == user.id)
        )
    ).scalar_one_or_none()
    if b is None:
        b = Budget(user_id=user.id)
        session.add(b)
        await session.commit()
        await session.refresh(b)
    return BudgetOut.model_validate(b)


@router.patch("/me/budget", response_model=BudgetOut)
async def patch_my_budget(
    body: BudgetPatch,
    user: Annotated[AuthenticatedUser, Depends(current_user)],
    session: Annotated[AsyncSession, Depends(get_session)],
) -> BudgetOut:
    b = (
        await session.execute(
            select(Budget).where(Budget.user_id == user.id)
        )
    ).scalar_one_or_none()
    if b is None:
        b = Budget(user_id=user.id)
        session.add(b)
        await session.flush()
    if body.daily_usd is not None:
        b.daily_usd = body.daily_usd
    if body.monthly_usd is not None:
        b.monthly_usd = body.monthly_usd
    if body.max_shots_per_job is not None:
        b.max_shots_per_job = body.max_shots_per_job
    await audit.record(
        session, user_id=user.id, action="budget.updated",
        target_type="budget", target_id=user.id,
    )
    await session.commit()
    await session.refresh(b)
    return BudgetOut.model_validate(b)


@router.post(
    "/me/api-keys",
    response_model=ApiKeyCreated,
    status_code=status.HTTP_201_CREATED,
)
async def create_my_api_key(
    body: CreateApiKey,
    user: Annotated[AuthenticatedUser, Depends(current_user)],
    session: Annotated[AsyncSession, Depends(get_session)],
) -> ApiKeyCreated:
    plaintext, prefix, hashed = new_api_key()
    row = ApiKey(user_id=user.id, prefix=prefix, hash=hashed, label=body.label)
    session.add(row)
    await audit.record(
        session, user_id=user.id, action="api_key.created",
        target_type="api_key", target_id=row.id,
        detail={"label": body.label, "prefix": prefix},
    )
    await session.commit()
    await session.refresh(row)
    out = ApiKeyOut.model_validate(row).model_dump()
    out["plaintext"] = plaintext
    return ApiKeyCreated(**out)


@router.get("/me/api-keys", response_model=list[ApiKeyOut])
async def list_my_api_keys(
    user: Annotated[AuthenticatedUser, Depends(current_user)],
    session: Annotated[AsyncSession, Depends(get_session)],
) -> list[ApiKeyOut]:
    rows = (
        await session.execute(
            select(ApiKey).where(ApiKey.user_id == user.id)
        )
    ).scalars().all()
    return [ApiKeyOut.model_validate(r) for r in rows]


@router.delete(
    "/me/api-keys/{key_id}",
    status_code=status.HTTP_204_NO_CONTENT,
    responses={404: {}},
)
async def revoke_my_api_key(
    key_id: uuid.UUID,
    user: Annotated[AuthenticatedUser, Depends(current_user)],
    session: Annotated[AsyncSession, Depends(get_session)],
) -> None:
    row = (
        await session.execute(
            select(ApiKey).where(
                ApiKey.id == key_id, ApiKey.user_id == user.id
            )
        )
    ).scalar_one_or_none()
    if row is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND)
    row.revoked_at = datetime.now(timezone.utc)
    await audit.record(
        session, user_id=user.id, action="api_key.revoked",
        target_type="api_key", target_id=key_id,
    )
    await session.commit()


# ---------- Admin ----------


@router.post(
    "/admin/users",
    response_model=UserOut,
    status_code=status.HTTP_201_CREATED,
)
async def admin_create_user(
    body: CreateUser,
    _: Annotated[AuthenticatedUser, Depends(require_admin)],
    session: Annotated[AsyncSession, Depends(get_session)],
) -> UserOut:
    existing = (
        await session.execute(select(User).where(User.email == body.email))
    ).scalar_one_or_none()
    if existing is not None:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT, detail="email already exists"
        )
    u = User(
        email=body.email,
        password_hash=hash_password(body.password),
        role=body.role,
    )
    session.add(u)
    await session.flush()
    session.add(Budget(user_id=u.id))
    await session.commit()
    await session.refresh(u)
    return UserOut.model_validate(u)


@router.get("/admin/users", response_model=list[UserOut])
async def admin_list_users(
    _: Annotated[AuthenticatedUser, Depends(require_admin)],
    session: Annotated[AsyncSession, Depends(get_session)],
) -> list[UserOut]:
    rows = (await session.execute(select(User))).scalars().all()
    return [UserOut.model_validate(r) for r in rows]
