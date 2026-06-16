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
    """Returns (full_plaintext, prefix, hash). Caller stores prefix +
    hash; gives plaintext to the user once.
    """
    prefix = "".join(secrets.choice(_ALPHABET) for _ in range(KEY_PREFIX_LEN))
    body = "".join(secrets.choice(_ALPHABET) for _ in range(_KEY_BODY_LEN))
    plaintext = f"{prefix}.{body}"
    return plaintext, prefix, _context.hash(plaintext)


def hash_api_key(plaintext: str) -> str:
    return _context.hash(plaintext)


def verify_api_key(plaintext: str, hashed: str) -> bool:
    try:
        return _context.verify(plaintext, hashed)
    except ValueError:
        return False


def split_prefix(plaintext: str) -> str | None:
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
