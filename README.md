# quantum-stack

The **Photon · Phonon · Spinor** quantum compiler stack, plus a FastAPI
job service, a React + Monaco playground, and a Docker compose
deployment. Write a quantum program once; run it on any chip.

[![docs](https://img.shields.io/badge/docs-nimesh08.github.io%2Fquantum--stack-4f46e5?style=flat-square)](https://nimesh08.github.io/quantum-stack/)
[![phase-d-ci](https://github.com/nimesh08/quantum-stack/actions/workflows/phase-d-ci.yml/badge.svg)](https://github.com/nimesh08/quantum-stack/actions/workflows/phase-d-ci.yml)
[![docs-ci](https://github.com/nimesh08/quantum-stack/actions/workflows/docs.yml/badge.svg)](https://github.com/nimesh08/quantum-stack/actions/workflows/docs.yml)

> **Documentation: <https://nimesh08.github.io/quantum-stack/>** —
> landing, quickstart, four tutorials, full prose guide, **REST
> reference** (Redoc), **Python reference** (mkdocstrings: every
> public symbol with parameters / returns / raises / examples),
> **TypeScript reference** (TypeDoc), decisions log, build journal,
> glossary.

---

## What's here

The stack has four phases plus a platform layer; all five are
implemented and tested:

```
Photon         OO language + photon.lib (Phase C)
  v
Phonon         structured IR + optimizer (Phase B)
  v
Spinor         portable / chip-locked assembly (Phase A)
  v
provider       IBM, AWS, Azure, direct vendor APIs

  ^----- Phase D platform wraps the whole stack -----^
       FastAPI jobsvc · React playground · nightly calibration · Docker
```

Tests at end of each phase:

| Phase | What it ships | Tests |
|---|---|---|
| A — Spinor | C++ compiler, MLIR-style dialect, parser, placement, routing (SABRE), decomposition (KAK + Euler ZYZ), QASM3/QIR/Quil emitters, IBM/AWS/Azure/local submission adapters | 116 |
| B — Phonon | Control-flow IR, linear type checker (no-cloning), optimizer pipeline (cancellation, rotation merging, ZX, scheduling) | 17 |
| C — Photon | OO language + photon.lib, three frontends (`.pho`, `@photon.kernel`, Clang LibTooling) converging on one engine via nanobind | 13 |
| D — Platform | FastAPI 0.137.1 jobsvc, APScheduler calibration, React 19.2 + Monaco playground, Postgres 17.10, Docker compose | 107 |
| **Total** | | **253** |

---

## What's next

The roadmap for chip coverage and new modalities lives in
[`FUTUREPLAN.md`](FUTUREPLAN.md) (also rendered on the docs site at
<https://nimesh08.github.io/quantum-stack/futureplan/>). **Steps 1
and 2 are landed in this commit:** the registry now ships **26
chips** (up from 4) covering IBM Heron r2/r3, Eagle (Brisbane,
Sherbrooke), Osprey, Nighthawk r1, Torrino, Quantinuum H1-1 / H2-1
/ Helios, IonQ Tempo / Forte / Forte Enterprise / Aria-1 / Harmony /
Aria-proto, Rigetti Ankaa-2 / Ankaa-3 / Ankaa-9Q-3, IQM Garnet /
Emerald, OQC Toshiko, AQT Pine, plus four new submit adapters for
QCI Aqumen, Anyon Yukon, TII Falcon, and Alice & Bob Boson 4. Chips
we **cannot** yet support (and the exact piece of data we are
missing for each) are in
[`docs/site/content/chips_unsupported.md`](docs/site/content/chips_unsupported.md).

---

## Try it in 30 seconds

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack/platform/deploy
cp .env.example .env
./run.sh up -d                                  # builds & starts the whole stack
open http://localhost:8080                      # log in: admin@local / admin-password
```

Click **Run** in the playground — the default Bell program returns a
`00 / 11` histogram. That single click drives every layer.

For full details: <https://nimesh08.github.io/quantum-stack/quickstart/>.

---

## Repository layout

```
quantum-stack/
├── CMakeLists.txt          # top-level C++ build (LLVM/MLIR)
├── cmake/Versions.cmake    # pinned third-party versions
├── spinor/                 # Phase A — chip-locking
├── phonon/                 # Phase B — IR + optimizer
├── photon/                 # Phase C — language + frontends
├── platform/               # Phase D — FastAPI + worker + scheduler + SPA
│   ├── jobsvc/
│   ├── calibration/
│   ├── playground/
│   └── deploy/
├── docs/
│   ├── site/               # MkDocs Material — published to GitHub Pages
│   ├── build/              # progress logs, decisions, per-phase guides
│   └── specs/              # the six specification documents
└── .github/workflows/
```

---

## The two quantum-specific seams

Most of Phase D is conventional web infrastructure — assembled, not
invented. Two pieces are quantum-specific:

1.  **Cost control** — every submission consults the compiler's
    `ResourceEstimate`, multiplies `shots × chip.pricing.per_shot_usd`,
    rejects over-budget jobs **before** spending. See
    [`jobsvc.cost.check_budget`](https://nimesh08.github.io/quantum-stack/api/python/jobsvc/cost/#jobsvc.cost.check_budget).
2.  **Nightly calibration refresh** — APScheduler hits each provider,
    atomically replaces the per-chip JSON the compiler reads. See
    [`calibration.main.refresh_one`](https://nimesh08.github.io/quantum-stack/api/python/calibration/main/#calibration.main.refresh_one).

---

## The seven critical rules (master copy)

> **RULE 1** — Build bottom-up.
> Spinor → Phonon → Photon → Platform. A finished lower layer is a
> real, testable artifact the next layer depends on.
>
> **RULE 2** — Optimization lives in Phonon, never in Spinor.
>
> **RULE 3** — One C++ engine, one source of truth.
>
> **RULE 4** — Re-verify and pin every version before coding.
>
> **RULE 5** — Submit to providers in verbatim / pass-through mode only.
>
> **RULE 6** — Phase E (auto-synthesis) is out of scope.
>
> **RULE 7** — *Photon*, *Phonon*, *Spinor* are working names; run a
> trademark search before any public use.

---

## Pinned versions (re-verified 2026-06-16)

| Component | Pin | Authoritative source |
|---|---|---|
| LLVM / MLIR | 22.1.8 | [`cmake/Versions.cmake`](cmake/Versions.cmake) |
| C++ | C++20 | — |
| Eigen | 5.0.1 | gitlab.com/libeigen/eigen |
| nanobind | 2.12.0 | github.com/wjakob/nanobind |
| FastAPI | 0.137.1 | [`platform/jobsvc/pyproject.toml`](platform/jobsvc/pyproject.toml) |
| PostgreSQL | 17.10 | [`platform/deploy/docker-compose.yml`](platform/deploy/docker-compose.yml) |
| React | 19.2.7 | [`platform/playground/package.json`](platform/playground/package.json) |
| @monaco-editor/react | ^4.7.0 | same |
| MkDocs Material | 9.7.6 | [`docs/site/pyproject.toml`](docs/site/pyproject.toml) |

---

## License

TBD before any public release. See RULE 7: trademark search required
before public use of the working names.
