# heisenberg

The single-command launcher for the Heisenberg Quantum Stack.

```bash
pip install heisenberg
heisenberg init      # creates ~/.local/share/heisenberg/, runs migrations
heisenberg seed      # creates admin@local with default API key
heisenberg run       # starts jobsvc + worker + scheduler, opens browser
```

`heisenberg run` runs the FastAPI service, the queue worker, and the
APScheduler calibration job in one process, against a SQLite database
at `~/.local/share/heisenberg/jobsvc.db`. The built playground SPA is
served at `http://127.0.0.1:8080/`.

For production:

```bash
heisenberg run --production --postgres postgresql+asyncpg://USER:PASS@HOST/DB
```

Splits the worker and scheduler into child processes, binds `0.0.0.0`,
emits JSON logs, and uses Postgres instead of SQLite.

Heisenberg was designed and implemented by **Nimesh Cheedella**.
