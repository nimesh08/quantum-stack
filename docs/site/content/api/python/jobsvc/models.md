# `jobsvc.models`

SQLAlchemy 2.0 ORM. Cross-dialect helpers (`GUID`, JSON-with-JSONB
fallback) so the same models work against Postgres 17 (production)
and SQLite (unit tests).

## Enums

::: jobsvc.models.JobState
    options:
      show_root_full_path: false

::: jobsvc.models.SourceKind
    options:
      show_root_full_path: false

::: jobsvc.models.UserRole
    options:
      show_root_full_path: false

## State machine

::: jobsvc.models.LEGAL_TRANSITIONS
    options:
      show_root_full_path: false

::: jobsvc.models.TERMINAL_STATES
    options:
      show_root_full_path: false

::: jobsvc.models.IllegalTransitionError
    options:
      show_root_full_path: false

## Tables

::: jobsvc.models.User
    options:
      members: []
      show_root_full_path: false

::: jobsvc.models.ApiKey
    options:
      members: []
      show_root_full_path: false

::: jobsvc.models.Budget
    options:
      members: []
      show_root_full_path: false

::: jobsvc.models.Job
    options:
      members:
        - transition
        - is_terminal
      show_root_full_path: false

::: jobsvc.models.Result
    options:
      members: []
      show_root_full_path: false

::: jobsvc.models.CalibrationSnapshot
    options:
      members: []
      show_root_full_path: false

::: jobsvc.models.AuditLog
    options:
      members: []
      show_root_full_path: false
