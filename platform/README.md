# Platform — services on top of the compiler

The product layer that turns the Photon · Phonon · Spinor compiler
into something a person can actually use:

| Folder         | Purpose                                                |
|----------------|--------------------------------------------------------|
| `jobsvc/`      | FastAPI service + workers + queue.                     |
| `calibration/` | Nightly refresh job (APScheduler).                     |
| `playground/`  | React 19.2 + Monaco SPA (built into `jobsvc/static/`). |
| `launcher/`    | The `heisenberg` CLI — single-command launch.          |
| `systemd/`     | Production unit files for native server installs.      |

## Run it

```bash
pip install heisenberg
heisenberg init      # creates ~/.local/share/heisenberg/, runs migrations
heisenberg seed      # creates admin@local with default API key
heisenberg run       # starts jobsvc + worker + scheduler, opens browser
```

By default this runs against SQLite at
`~/.local/share/heisenberg/jobsvc.db`. For production:

```bash
heisenberg run --production --postgres postgresql+asyncpg://USER:PASS@HOST/DB
```

The systemd path is documented in [`systemd/README.md`](systemd/README.md).

## Two quantum-specific seams (everything else is conventional)

1. **Cost control** — before queueing, the `ResourceEstimate` from
   the compiler is multiplied by `chip.pricing.per_shot_usd` and
   compared to the user's `Budget`. Over-budget → 402, **before**
   spending.
2. **Nightly calibration refresh** — APScheduler hits each provider
   and writes `~/.local/share/heisenberg/calibration/<chip>.json` —
   the path the chip YAMLs declare as their calibration store.

## Read first

- [Operations guide](../docs/site/content/operations/index.md)
- [Future plan + chip-coverage roadmap](../FUTUREPLAN.md)
- [Unsupported chips ledger](../docs/site/content/chips_unsupported.md)
