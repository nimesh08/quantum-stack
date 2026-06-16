"""GET /api/v1/targets — registry of supported chips."""

from __future__ import annotations

from fastapi import APIRouter

from ..registry import list_chips, get_chip
from ..schemas import TargetOut


router = APIRouter(prefix="/api/v1", tags=["targets"])


@router.get("/targets", response_model=list[TargetOut])
async def get_targets() -> list[TargetOut]:
    items = []
    # Always include "generic" as a synthetic target.
    g = get_chip("generic")
    if g:
        items.append(TargetOut(**g.to_public()))
    for c in list_chips():
        items.append(TargetOut(**c.to_public()))
    return items


@router.get("/targets/{chip_id}", response_model=TargetOut, responses={404: {}})
async def get_target(chip_id: str) -> TargetOut:
    c = get_chip(chip_id)
    if c is None:
        from fastapi import HTTPException, status
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND)
    return TargetOut(**c.to_public())
