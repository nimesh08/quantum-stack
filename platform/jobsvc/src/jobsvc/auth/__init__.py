"""auth/__init__ — re-exports."""
from .passwords import hash_password, verify_password
from .jwt import create_token_pair, decode_token, JWTError
from .api_key import (
    new_api_key,
    hash_api_key,
    verify_api_key,
    KEY_PREFIX_LEN,
)
from .deps import (
    current_user,
    require_admin,
    optional_user,
    AuthenticatedUser,
)

__all__ = [
    "AuthenticatedUser",
    "JWTError",
    "KEY_PREFIX_LEN",
    "create_token_pair",
    "current_user",
    "decode_token",
    "hash_api_key",
    "hash_password",
    "new_api_key",
    "optional_user",
    "require_admin",
    "verify_api_key",
    "verify_password",
]
