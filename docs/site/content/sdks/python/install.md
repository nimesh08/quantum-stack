# Python SDK — install

The Python SDK supports Python 3.12 or newer.

## The simple path

```bash
pip install heisenberg
```

This pulls `heisenberg`, `heisenberg-photon`, `heisenberg-spinor-submit`,
`heisenberg-jobsvc`, and `heisenberg-calibration` in one go. After
this you have the `heisenberg` launcher, the `photon` Python module,
the `spinor_submit` adapters, and the `jobsvc` library available
under their import names.

## In a virtual environment

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install heisenberg
```

Recommended for application code so the install does not pollute
your system Python.

## With Postgres extras

```bash
pip install 'heisenberg[postgres]'
```

Adds `asyncpg` so `heisenberg run --postgres URL` works without a
second install.

## With test extras

```bash
pip install 'heisenberg-jobsvc[test]'
```

Pulls `pytest`, `pytest-asyncio`, `respx`, `freezegun`,
`testcontainers[postgres]`. Use this when contributing.

## Conda / mamba

```bash
mamba create -n heisenberg python=3.13
mamba activate heisenberg
pip install heisenberg
```

Heisenberg is not yet on conda-forge; install via pip inside a
conda env.

## Installing only the small pieces

If you do not want the launcher, install the package you need
directly:

```bash
pip install heisenberg-photon            # Photon kernels only
pip install heisenberg-spinor-submit     # provider adapters only
pip install heisenberg-jobsvc            # the FastAPI app as a library
```

The dependency graph is intentional: `heisenberg-photon` is enough
to compile and run a kernel locally. `heisenberg-jobsvc` is enough
to mount the HTTP service inside a bigger app.

## Verify

```bash
python -c "import photon, spinor_submit, jobsvc, calibration, heisenberg; \
           print('photon       :', photon.__version__); \
           print('spinor_submit:', spinor_submit.__name__); \
           print('jobsvc       :', jobsvc.__version__); \
           print('calibration  :', calibration.__version__); \
           print('heisenberg   :', heisenberg.__version__)"
```

If any import fails, see [Operations / Troubleshooting](../../operations/troubleshooting.md).

---

Heisenberg's Python SDK was designed and implemented by **Nimesh Cheedella**.
