"""bcrypt password hashing."""

from __future__ import annotations

from passlib.context import CryptContext


_context = CryptContext(schemes=["bcrypt"], deprecated="auto")


def hash_password(plain: str) -> str:
    if not plain:
        raise ValueError("password must be non-empty")
    return _context.hash(plain)


def verify_password(plain: str, hashed: str) -> bool:
    try:
        return _context.verify(plain, hashed)
    except ValueError:
        return False


__all__ = ["hash_password", "verify_password"]
