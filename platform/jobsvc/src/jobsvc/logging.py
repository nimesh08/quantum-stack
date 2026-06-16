"""structlog JSON logger setup. Importing this module configures the
root logger; safe to call repeatedly.
"""

from __future__ import annotations

import logging
import sys

import structlog

from .config import get_settings


_CONFIGURED = False


def configure() -> None:
    global _CONFIGURED
    if _CONFIGURED:
        return
    s = get_settings()
    level = getattr(logging, s.log_level.upper(), logging.INFO)
    logging.basicConfig(
        level=level, stream=sys.stdout, format="%(message)s"
    )
    processors: list = [
        structlog.contextvars.merge_contextvars,
        structlog.stdlib.add_log_level,
        structlog.processors.TimeStamper(fmt="iso"),
        structlog.processors.StackInfoRenderer(),
        structlog.processors.format_exc_info,
    ]
    if s.log_json:
        processors.append(structlog.processors.JSONRenderer())
    else:
        processors.append(structlog.dev.ConsoleRenderer())
    structlog.configure(
        processors=processors,
        wrapper_class=structlog.make_filtering_bound_logger(level),
        cache_logger_on_first_use=True,
    )
    _CONFIGURED = True


def get_logger(name: str = "jobsvc") -> structlog.stdlib.BoundLogger:
    configure()
    return structlog.get_logger(name)


__all__ = ["configure", "get_logger"]
