"""bcrypt password hashing."""

from __future__ import annotations

from passlib.context import CryptContext


_context = CryptContext(schemes=["bcrypt"], deprecated="auto")


def hash_password(plain: str) -> str:
    """Hash a password for storage.

    Args:
        plain: Plain-text password. Must be non-empty.

    Returns:
        bcrypt hash suitable for ``users.password_hash``.

    Raises:
        ValueError: ``plain`` is empty.
    """
    if not plain:
        raise ValueError("password must be non-empty")
    return _context.hash(plain)


def verify_password(plain: str, hashed: str) -> bool:
    """Constant-time password comparison.

    Args:
        plain: User-supplied password.
        hashed: bcrypt hash from the database.

    Returns:
        True iff ``plain`` matches ``hashed``. Never raises on
        malformed hashes — returns False instead.
    """
    try:
        return _context.verify(plain, hashed)
    except ValueError:
        return False


__all__ = ["hash_password", "verify_password"]
