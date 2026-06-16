# `jobsvc.routers`

The HTTP endpoints. For full request/response shapes browse the
[REST reference](../../rest/index.md); these pages document the
Python implementations behind each endpoint.

## `routers.jobs`

The `/api/v1/jobs` family — the heart of the service.

::: jobsvc.routers.jobs
    options:
      members_order: source
      show_root_full_path: false
      filters:
        - "!^_"

## `routers.users`

`/me`, `/me/budget`, `/me/api-keys`, `/admin/users`.

::: jobsvc.routers.users
    options:
      members_order: source
      show_root_full_path: false
      filters:
        - "!^_"

## `routers.login`

::: jobsvc.routers.login
    options:
      members_order: source
      show_root_full_path: false
      filters:
        - "!^_"

## `routers.targets`

::: jobsvc.routers.targets
    options:
      members_order: source
      show_root_full_path: false
      filters:
        - "!^_"

## `routers.health`

::: jobsvc.routers.health
    options:
      members_order: source
      show_root_full_path: false
      filters:
        - "!^_"
