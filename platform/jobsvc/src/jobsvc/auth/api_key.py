"""API-key generation + verification.

Format: 8-char public prefix + `.` + 32-char body, e.g.
`Q4r2p8aA.GvXc...32chars`. The prefix lets us index keys without
storing plaintext.
"""

from __future__ import annotations

import secrets
import string
from passlib.context import CryptContext

KEY_PREFIX_LEN = 8
_KEY_BODY_LEN = 32
_ALPHABET = string.ascii_letters + string.digits

_context = CryptContext(schemes=["bcrypt"], deprecated="auto")


def new_api_key() -> tuple[str, str, str]:
    """Mint a brand new API key.

    The plaintext is shown to the user **once** — at creation. We
    store only the prefix (for indexed lookup) and the bcrypt hash.

    Returns:
        ``(plaintext, prefix, hash)``. Plaintext format is
        ``<8-char-prefix>.<32-char-body>``. Persist ``prefix`` and
        ``hash`` on
        [`ApiKey`][jobsvc.models.ApiKey]; return ``plaintext`` to the
        caller exactly once.
    """
    prefix = "".join(secrets.choice(_ALPHABET) for _ in range(KEY_PREFIX_LEN))
    body = "".join(secrets.choice(_ALPHABET) for _ in range(_KEY_BODY_LEN))
    plaintext = f"{prefix}.{body}"
    return plaintext, prefix, _context.hash(plaintext)


def hash_api_key(plaintext: str) -> str:
    """Hash a plaintext API key for storage."""
    return _context.hash(plaintext)


def verify_api_key(plaintext: str, hashed: str) -> bool:
    """Constant-time check of an inbound key against a stored hash."""
    try:
        return _context.verify(plaintext, hashed)
    except ValueError:
        return False


def split_prefix(plaintext: str) -> str | None:
    """Return the 8-char prefix of a well-formed API key, else ``None``.

    Used to scope the bcrypt-verify loop to only the keys whose
    prefix matches — a database index can't help with bcrypt
    comparison, but an index on ``prefix`` cuts the candidate set to
    ~1 in practice.
    """
    if "." not in plaintext:
        return None
    p, _, _ = plaintext.partition(".")
    if len(p) != KEY_PREFIX_LEN:
        return None
    return p


__all__ = [
    "KEY_PREFIX_LEN",
    "hash_api_key",
    "new_api_key",
    "split_prefix",
    "verify_api_key",
]
