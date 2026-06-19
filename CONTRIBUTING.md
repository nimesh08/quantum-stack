# Contributing to Heisenberg Quantum Stack

Thanks for your interest. Patches, bug reports, and chip-coverage
PRs are all welcome.

Heisenberg, Spinor, Phonon and Photon were designed and implemented
by **Nimesh Cheedella**. Contributions are integrated under the
project's [Apache-2.0](LICENSE) license.

## Quick start for contributors

```bash
# Clone.
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack

# Python: launcher + jobsvc + calibration + spinor-submit, editable installs.
python3 -m venv .venv
. .venv/bin/activate
pip install -e ./platform/launcher \
            -e ./platform/jobsvc \
            -e ./platform/calibration \
            -e ./spinor/submit/python

# Init + seed + run.
heisenberg init
heisenberg seed
heisenberg run
```

For the C++ engine: see
[SDKs / C++ / Install / From source](https://nimesh08.github.io/quantum-stack/sdks/cpp/install/#from-source).

## Test commands

| What | Command |
|------|---------|
| Python tests for jobsvc, calibration, spinor_submit | `pytest platform/jobsvc/tests platform/calibration/tests spinor/submit/python/tests` |
| Playground unit tests | `cd platform/playground && npm test` |
| Playground typecheck | `cd platform/playground && npm run typecheck` |
| C++ engine tests | `cmake -B build && cmake --build build && ctest --test-dir build` |
| Docs build | `cd docs/site && mkdocs build --strict` |
| Docs lint | `python scripts/check_docs.py` |
| Cursor scrub | `bash scripts/check_no_cursor.sh` |

## Adding a new chip

The whole point of the Spinor architecture is that adding a new
chip is a **YAML-only** change.

1. Copy an existing YAML from
   [`spinor/registry/chips/`](spinor/registry/chips/) and edit
   `id`, `provider`, `qubits`, `native_gates`, `coupling_map`,
   `pricing`, and `notes` (with the upstream URL + verified date).
2. If the chip uses a new topology, add a YAML under
   [`spinor/registry/topologies/`](spinor/registry/topologies/).
3. Run `python scripts/verify_chip_yamls.py` — it cross-checks the
   YAML against the C++ Lexer's gate set and the topology
   directory, then runs a smoke compile.
4. Open a PR.

If your chip's data is not publicly verifiable, do **not** invent
fields — instead add a row to
[`docs/site/content/internals/chips_unsupported.md`](docs/site/content/internals/chips_unsupported.md)
naming the missing piece.

## PR conventions

- One commit per PR for small changes; squash-merge larger ones on
  GitHub.
- Commit message: short imperative subject, blank line, then a
  paragraph explaining the *why*.
- Update the documentation in the same PR. The
  `scripts/check_docs.py` linter blocks broken-link PRs.
- Tests live next to the code they test. Add tests for any new
  behaviour.

## What is in scope

| Yes | No |
|-----|-----|
| Bug fixes anywhere in the stack. | Phase E (auto-synthesis); see [RULE 6](docs/site/content/internals/seven_rules.md). |
| New chips on existing providers (one YAML each). | Vendor SDKs whose APIs are not publicly documented. |
| New optimiser passes in Phonon. | Optimisation in Spinor (see [RULE 2](docs/site/content/internals/seven_rules.md)). |
| New emitters in Spinor. | Anything that reimplements compilation outside the C++ engine (see [RULE 3](docs/site/content/internals/seven_rules.md)). |
| Documentation improvements. | Marketing material that name-checks "Photon", "Phonon", or "Spinor" without a trademark search ([RULE 7](docs/site/content/internals/seven_rules.md)). |

## Communication

- Bugs and feature requests:
  <https://github.com/nimesh08/quantum-stack/issues>.
- Security disclosures: see [`SECURITY.md`](SECURITY.md). Do not
  open a public issue for security problems.

## Code of conduct

By participating in this project you agree to abide by the
[Code of Conduct](CODE_OF_CONDUCT.md).
