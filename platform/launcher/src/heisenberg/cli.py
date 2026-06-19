"""``heisenberg`` command-line interface.

Subcommands:

* ``heisenberg init``   - create the data directory + run migrations
* ``heisenberg seed``   - create the default admin user + API key
* ``heisenberg run``    - boot jobsvc + worker + scheduler; open browser
* ``heisenberg stop``   - stop a backgrounded ``heisenberg run``
* ``heisenberg playground build`` - rebuild the SPA into ``jobsvc/static/``
* ``heisenberg version`` - print package versions

Author: Nimesh Cheedella <chnimesh0808@gmail.com>
"""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import time
from pathlib import Path

import click

from . import __version__, paths
from .env import apply_environment, find_spinor_registry
from .setup import init_data_dir, run_migrations, seed_sync


@click.group(help="Heisenberg Quantum Stack launcher.")
@click.version_option(__version__, prog_name="heisenberg")
def main() -> None:
    pass


@main.command(help="Create the data directory and run database migrations.")
@click.option(
    "--postgres",
    "database_url",
    default=None,
    metavar="URL",
    help="SQLAlchemy URL for Postgres (e.g. postgresql+asyncpg://...). "
    "Defaults to SQLite at ~/.local/share/heisenberg/jobsvc.db.",
)
def init(database_url: str | None) -> None:
    apply_environment(database_url=database_url)
    d = init_data_dir()
    click.echo(f"data dir:   {d}")
    click.echo(f"database:   {os.environ['JOBSVC_DATABASE_URL']}")
    if reg := find_spinor_registry():
        click.echo(f"registry:   {reg}")
    else:
        click.secho(
            "warning: spinor registry not found; chip submissions will fail",
            fg="yellow",
        )
    click.echo("running database migrations ...")
    run_migrations()
    click.secho("ok.", fg="green")


@main.command(help="Create the default admin user (admin@local) + API key.")
@click.option(
    "--postgres",
    "database_url",
    default=None,
    metavar="URL",
    help="Override the database URL.",
)
def seed(database_url: str | None) -> None:
    apply_environment(database_url=database_url)
    email, key = seed_sync()
    click.echo(f"user:    {email}")
    click.echo(f"password: admin-password   (change in the playground)")
    click.echo(f"api key: {key}")
    if key == "<existing>":
        click.secho(
            "user already existed; no new API key was issued. "
            "Use the playground to mint a fresh key.",
            fg="yellow",
        )


@main.command(help="Boot jobsvc + worker + scheduler. Opens the playground.")
@click.option("--host", default="127.0.0.1", show_default=True)
@click.option("--port", default=8080, show_default=True, type=int)
@click.option(
    "--postgres",
    "database_url",
    default=None,
    metavar="URL",
    help="Use Postgres instead of SQLite.",
)
@click.option(
    "--production/--dev",
    default=False,
    help="Production mode: bind 0.0.0.0, JSON logs, no browser open.",
)
@click.option("--no-browser", is_flag=True, help="Do not open the browser.")
@click.option(
    "--background",
    is_flag=True,
    help="Detach into the background; write PID to the data dir.",
)
@click.option(
    "--log-level",
    default="info",
    show_default=True,
    type=click.Choice(["debug", "info", "warning", "error"], case_sensitive=False),
)
@click.option("--no-worker", is_flag=True, help="Disable the in-process worker.")
@click.option(
    "--no-scheduler",
    is_flag=True,
    help="Disable the calibration scheduler.",
)
def run(
    host: str,
    port: int,
    database_url: str | None,
    production: bool,
    no_browser: bool,
    background: bool,
    log_level: str,
    no_worker: bool,
    no_scheduler: bool,
) -> None:
    if production:
        host = "0.0.0.0"
        no_browser = True
    apply_environment(database_url=database_url, log_json=production)

    if background:
        _spawn_background(host, port, production, log_level)
        return

    from .supervisor import clear_pid, run_in_process, write_pid

    write_pid()
    try:
        click.echo(f"heisenberg {__version__} on http://{host}:{port}/")
        click.echo(f"data dir:  {paths.data_dir()}")
        click.echo(f"database:  {os.environ['JOBSVC_DATABASE_URL']}")
        click.echo("press Ctrl-C to stop.")
        run_in_process(
            host=host,
            port=port,
            open_browser=not no_browser,
            log_level=log_level,
            enable_worker=not no_worker,
            enable_scheduler=not no_scheduler,
        )
    finally:
        clear_pid()


def _spawn_background(host: str, port: int, production: bool, log_level: str) -> None:
    log = paths.log_file()
    pid_file = paths.pid_file()
    if pid_file.exists():
        existing = pid_file.read_text(encoding="utf-8").strip()
        click.secho(
            f"a heisenberg process appears to be running (pid {existing}); "
            f"run `heisenberg stop` first.",
            fg="yellow",
        )
        sys.exit(1)
    args = [
        sys.executable,
        "-m",
        "heisenberg",
        "run",
        "--host",
        host,
        "--port",
        str(port),
        "--no-browser",
        "--log-level",
        log_level,
    ]
    if production:
        args.append("--production")
    click.echo(f"starting heisenberg in the background; logs at {log}")
    log_fp = open(log, "ab")
    proc = subprocess.Popen(  # noqa: S603
        args,
        stdout=log_fp,
        stderr=log_fp,
        stdin=subprocess.DEVNULL,
        env=os.environ.copy(),
        start_new_session=True,
    )
    pid_file.write_text(str(proc.pid), encoding="utf-8")
    click.echo(f"pid {proc.pid} -> {pid_file}")


@main.command(help="Stop a backgrounded `heisenberg run`.")
def stop() -> None:
    pid_file = paths.pid_file()
    if not pid_file.exists():
        click.echo("no PID file; nothing to stop.")
        return
    try:
        pid = int(pid_file.read_text(encoding="utf-8").strip())
    except ValueError:
        click.secho("PID file is malformed; deleting it.", fg="yellow")
        pid_file.unlink(missing_ok=True)
        return
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        click.echo(f"pid {pid} not running; clearing PID file.")
        pid_file.unlink(missing_ok=True)
        return
    for _ in range(50):
        time.sleep(0.1)
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            break
    pid_file.unlink(missing_ok=True)
    click.secho("stopped.", fg="green")


@main.group(help="Manage the React playground bundle.")
def playground() -> None:
    pass


@playground.command("build", help="Rebuild the SPA into jobsvc/static/.")
def playground_build() -> None:
    here = Path(__file__).resolve()
    repo = _find_repo_root(here)
    if repo is None:
        click.secho(
            "could not locate the playground source tree from a wheel install; "
            "run this command from a source checkout.",
            fg="red",
        )
        sys.exit(1)
    pg = repo / "platform" / "playground"
    if not pg.is_dir():
        click.secho(f"no playground at {pg}", fg="red")
        sys.exit(1)
    click.echo(f"building {pg} ...")
    subprocess.check_call(["npm", "ci"], cwd=pg)  # noqa: S603, S607
    subprocess.check_call(["npm", "run", "build"], cwd=pg)  # noqa: S603, S607
    click.secho("ok.", fg="green")


def _find_repo_root(start: Path) -> Path | None:
    for p in [start, *start.parents]:
        if (p / "platform" / "playground").is_dir() and (p / "spinor").is_dir():
            return p
    return None


@main.command(help="Print version and key paths.")
def version() -> None:
    click.echo(f"heisenberg     {__version__}")
    try:
        import jobsvc

        click.echo(f"jobsvc         {jobsvc.__version__}")
    except Exception:  # noqa: BLE001
        click.echo("jobsvc         (not installed)")
    try:
        import calibration

        click.echo(f"calibration    {calibration.__version__}")
    except Exception:  # noqa: BLE001
        click.echo("calibration    (not installed)")
    click.echo(f"data dir       {paths.data_dir()}")
    if reg := find_spinor_registry():
        click.echo(f"registry       {reg}")
    else:
        click.echo("registry       (not found)")


if __name__ == "__main__":  # pragma: no cover
    main()
