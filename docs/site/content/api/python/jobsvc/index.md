# Python — `jobsvc`

The FastAPI job service. Twelve modules, all auto-generated from
docstrings via [mkdocstrings 1.0.4](https://mkdocstrings.github.io/).
Every public class, function, and dataclass is documented with
**Args / Returns / Raises / Examples**.

## Modules

<div class="grid cards" markdown>

-   :material-cog:{ .lg .middle } **[`jobsvc.engine`](engine.md)**

    ---

    Wrapper around the Phase C nanobind binding. The single entry
    point [`compile_program`][jobsvc.engine.compile_program] is what
    `POST /jobs` calls.

-   :material-cash-multiple:{ .lg .middle } **[`jobsvc.cost`](cost.md)**

    ---

    The cost-control seam.
    [`check_budget`][jobsvc.cost.check_budget] decides whether a job
    proceeds to `Queued`.

-   :material-database:{ .lg .middle } **[`jobsvc.models`](models.md)**

    ---

    SQLAlchemy 2.0 ORM. Includes the
    [`JobState`][jobsvc.models.JobState] enum and
    [`Job.transition`][jobsvc.models.Job.transition].

-   :material-truck-delivery:{ .lg .middle } **[`jobsvc.queue`](queue.md)**

    ---

    Postgres-as-queue: `FOR UPDATE SKIP LOCKED` claim,
    `LISTEN/NOTIFY` wakeup, lease reclaim.

-   :material-account-hard-hat:{ .lg .middle } **[`jobsvc.worker`](worker.md)**

    ---

    [`process_one`][jobsvc.worker.process_one] — claim → recompile
    → submit verbatim → store histogram.

-   :material-router-network:{ .lg .middle } **[`jobsvc.providers`](providers.md)**

    ---

    Routes `chip.provider` to the Phase A `spinor_submit` adapter
    (verbatim mode) or the in-process simulator.

-   :material-database-search:{ .lg .middle } **[`jobsvc.registry`](registry.md)**

    ---

    Reads `spinor/registry/chips/*.yaml` into
    [`ChipInfo`][jobsvc.registry.ChipInfo].

-   :material-shield-account:{ .lg .middle } **[`jobsvc.auth`](auth.md)**

    ---

    JWT + API-key + bcrypt. The `current_user` FastAPI dep tries
    both paths.

-   :material-form-textbox:{ .lg .middle } **[`jobsvc.schemas`](schemas.md)**

    ---

    Pydantic 2 request/response models. Wire format for the API.

-   :material-cog-outline:{ .lg .middle } **[`jobsvc.config`](config.md)**

    ---

    pydantic-settings. Every env var is `JOBSVC_*`.

-   :material-clipboard-text-clock:{ .lg .middle } **[`jobsvc.audit`](audit.md)**

    ---

    [`record`][jobsvc.audit.record] — append an audit-log row in the
    caller's transaction.

-   :material-chart-line:{ .lg .middle } **[`jobsvc.metrics`](metrics.md)**

    ---

    Prometheus counters, gauges, and histograms surfaced at
    `/metrics`.

-   :material-api:{ .lg .middle } **[`jobsvc.routers`](routers.md)**

    ---

    The HTTP endpoints — `jobs`, `users`, `targets`, `login`,
    `health`.

</div>
