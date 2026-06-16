"""Typed errors raised by the @photon.kernel translator."""

from __future__ import annotations


class PhotonKernelError(Exception):
    """Base class for all decorator-time errors."""


class UnsupportedConstructError(PhotonKernelError):
    """Raised when a Python construct cannot be translated to Phonon."""

    def __init__(self, what: str, lineno: int = 0, col: int = 0):
        msg = f"@photon.kernel cannot translate {what}"
        if lineno:
            msg += f" (line {lineno}, column {col})"
        super().__init__(msg)
        self.what = what
        self.lineno = lineno
        self.col = col


class CompilationError(PhotonKernelError):
    """Raised when the engine rejects the produced Phonon."""
