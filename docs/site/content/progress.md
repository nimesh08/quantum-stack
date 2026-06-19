---
title: Progress
---

# Progress

What has shipped, in plain English. The
[changelog](changelog.md) is the per-release audit trail; this page
is the readable summary. For the long-horizon roadmap see
[Vision](vision.md); for what we are working on right now see
[Current plan](plan.md).

## 0.5.0 — open-source release (June 2026)

The release that turns Heisenberg into a normal open-source
project anyone can install and run.

What you can do today:

- `pip install heisenberg && heisenberg run` brings up the
  service, the worker, the calibration scheduler, and the playground
  in one process. SQLite by default; opt into Postgres with
  `--postgres URL`.
- Submit a job from Python, C++, TypeScript, or any HTTP client.
  Three SDKs and a REST API, all backed by the same compiler.
- Read the documentation as a unified site:
  [Languages](languages/index.md),
  [SDKs](sdks/index.md),
  [Operations](operations/index.md),
  [Internals](internals/index.md), and full
  [auto-generated reference](reference/index.md) per language.
- Deploy to a real server with three systemd units
  ([`platform/systemd/`](https://github.com/nimesh08/quantum-stack/tree/main/platform/systemd))
  — no Docker required.
- Open a chip request, a feature request, or a security advisory
  through the GitHub templates.
- Trust the project's identity:
  [Apache-2.0 LICENSE](https://github.com/nimesh08/quantum-stack/blob/main/LICENSE),
  [AUTHORS](https://github.com/nimesh08/quantum-stack/blob/main/AUTHORS.md),
  [CONTRIBUTING](https://github.com/nimesh08/quantum-stack/blob/main/CONTRIBUTING.md),
  [CODE_OF_CONDUCT](https://github.com/nimesh08/quantum-stack/blob/main/CODE_OF_CONDUCT.md),
  [SECURITY](https://github.com/nimesh08/quantum-stack/blob/main/SECURITY.md).

## June 2026 — chip coverage step

27 chips across 8 vendors plus 4 cassette-only adapters waiting
for upstream REST URLs. The full table is in
[unsupported chips](internals/chips_unsupported.md).

## May 2026 — Photon (the user-facing language)

Object-oriented language with three frontends — `.pho` source, a
`@photon.kernel` Python decorator, and a `[[photon::kernel]]` C++
attribute — all converging on the same compiler engine via a
`nanobind` binding.

## May 2026 — Phonon (the structured IR)

Structured IR with linear types (no-cloning enforced at the type
level) and the optimizer pipeline (cancellation, rotation merging,
ZX simplification, scheduling).

## April 2026 — Spinor (the chip-locking compiler)

C++ compiler engine. MLIR-style dialect, recursive-descent parser,
placement, SABRE routing, KAK + Euler-ZYZ decomposition, OpenQASM
3 / QIR / Quil emitters. Submission adapters for IBM, AWS Braket,
and Azure Quantum in cassette mode.

---

For the full append-only history, including each milestone and
decision the way it was logged at build time, see
[Internals / Build journal](internals/build_journal.md).
