---
title: Unsupported chips
description: >
  Every chip we know about that we do not currently support, with the
  exact piece of data we are missing and what would unblock it. This
  page exists so a future maintainer can fix one row in one PR.
---

# Unsupported chips ledger

This page is the **honest list**. Every quantum chip we know about
that we cannot currently take a real production submission on, with
the exact piece of data we are missing and what would unblock it. We
update this in lockstep with the chip registry: if a row here moves
to "supported", the YAML for it lands in
[`spinor/registry/chips/`][chips] and the row disappears from this
table.

[chips]: https://github.com/nimesh08/quantum-stack/tree/main/spinor/registry/chips

> **Why this exists:** the user asked for it directly. If a chip's
> data is genuinely not public, we do **not** invent values in a YAML
> and call it shipped — instead the chip lands on this page with the
> exact missing data column populated, so anyone who *does* have the
> data can fix it in one PR.

## Compiler-recipe gaps (blocked on a Spinor decomposer change)

These chips have valid, verified YAMLs in the registry — they pass
the schema + topology + native-gate-vocabulary checks — but the
current C++ KAK two-qubit decomposer only ships recipes for `ecr`,
`ms`, and `rzz` entanglers. CZ-native and CX-native chips error out
at decomposition time with `emitCX: no recipe for entangler 'cz'`.

| Chip | Vendor | Modality | Reason | Exact missing data | What would unblock |
|---|---|---|---|---|---|
| `ibm_heron_r3` | IBM | gate-model SC | KAK-CZ recipe missing | `Decomposition::cxFromCz()` | one PR adding `H q[1]; CZ q[0],q[1]; H q[1]` lowering |
| `ibm_nighthawk_r1` | IBM | gate-model SC | KAK-CZ recipe missing | same | same |
| `rigetti_ankaa_3` | Rigetti | gate-model SC | KAK-CZ recipe missing | same | same |
| `rigetti_ankaa_2` | Rigetti | gate-model SC | KAK-CZ recipe missing | same | same |
| `rigetti_ankaa_9q_3` | Rigetti | gate-model SC | KAK-CZ recipe missing | same | same |
| `iqm_garnet` | IQM | gate-model SC | KAK-CZ recipe missing | same | same |
| `iqm_emerald` | IQM | gate-model SC | KAK-CZ recipe missing | same | same |
| `oqc_toshiko` | OQC | gate-model SC | KAK-CZ recipe missing | same | same |
| `qci_aqumen` | QCI | gate-model SC | KAK-CZ recipe missing **and** vendor blocker (see below) | same | same recipe + QCI live URL |
| `anyon_yukon` | Anyon | gate-model SC | KAK-CZ recipe missing **and** vendor blocker | same | same recipe + Anyon live URL |
| `tii_falcon` | TII | gate-model SC | KAK-CZ recipe missing **and** vendor blocker | same | same recipe + Qibolab host |
| `alicebob_boson_4` | Alice & Bob | cat-qubit | KAK-CX recipe missing **and** vendor blocker | `Decomposition::cxFromCx()` (passthrough) | identity recipe + Alice & Bob live URL |

**One PR unblocks all twelve.** Approximate effort: 1 day in
`spinor/passes/Decomposition.cpp` + ~6 unit tests against the
existing equivalence checker.

## Vendor-blocked (no public submission endpoint)

These chips have YAMLs and adapters, but the production submission
endpoint is not publicly documented. The adapter ships in cassette
mode only; live mode raises a clear `RuntimeError`.

| Chip | Vendor | Modality | Reason | Exact missing data | What would unblock |
|---|---|---|---|---|---|
| `qci_aqumen` | Quantum Circuits Inc. | dual-rail SC | live REST endpoint not public | submission URL + auth header format | QCI publishes their CUDA-Q backend's REST URL or signs us up to a beta |
| `anyon_yukon` | Anyon Technologies | gate-model SC | live REST endpoint not public | submission URL + token env var name | Anyon opens their cloud submission API |
| `tii_falcon` | TII | gate-model SC | no hosted production service | a Qibolab host the user controls | user deploys Qibolab; `QIBOLAB_HOST` gets set |
| `alicebob_boson_4` | Alice & Bob | cat-qubit | live REST endpoint not public | submission URL + token env var name | Alice & Bob opens their cloud beyond emulator |

## Decommissioned / preview-only

| Chip | Vendor | Reason | What this means today |
|---|---|---|---|
| `ionq_harmony` | IonQ | Decommissioning announced on AWS Braket; some regions already removed. | YAML retained for the cassette path; live mode will return 4xx from Braket. |
| `ibm_osprey` | IBM | 433-qubit research-class device; not on Premium pay-as-you-go. | YAML retained so an internal token-holder can submit; pricing field is conservative. |
| `aqt_pine` | AQT | Direct AQT API exists but no first-party submit adapter yet; provider is `local` until the AQT adapter is added. | Use cassette mode for tests; live mode is documented as future work. |

## Chips we know exist but do not yet ship a YAML for

If you need one of these, the recipe is: open a PR adding the YAML
under `spinor/registry/chips/`, with a citation to the upstream page
in the `notes` block, and the verification script
([`scripts/verify_chip_yamls.py`](https://github.com/nimesh08/quantum-stack/blob/main/scripts/verify_chip_yamls.py))
will tell you if the schema / native gates / topology check pass.

| Chip | Vendor | Modality | Why we did not ship today | What we would need to write the YAML |
|---|---|---|---|---|
| QuEra Aquila (256 atoms) | QuEra | analog Rydberg | not gate-model — needs the new Rydberg DSL (Step 3) | n/a — different DSL |
| Pasqal Fresnel | Pasqal | analog Rydberg | same | n/a |
| Pasqal MGX (gate-mode preview) | Pasqal | gate-model neutral atom | gate set + topology not yet finalised by upstream | published native gate set on the Pasqal cloud docs |
| Atom Computing (gate-mode) | Atom Computing | gate-model neutral atom | gate set not finalised in public docs | published native gate set + REST endpoint |
| Xanadu Borealis | Xanadu | photonic CV | not gate-model — needs photonic DSL (Step 5) | n/a |
| Xanadu X-series | Xanadu | photonic CV | same | n/a |
| ORCA PT-1 | ORCA | photonic discrete | needs photonic DSL (Step 5) | n/a |
| D-Wave Advantage 6.4 / Advantage2 | D-Wave | annealing | not a circuit — needs annealing DSL (Step 6) | n/a |
| D-Wave Advantage2 gate-mode preview | D-Wave | gate-model SC | gate set not in public docs | published native gate set |
| Microsoft Majorana 1 | Microsoft | topological | device not yet user-reachable | Azure target endpoint + native gate set |
| Diraq SiMOS-8 | Diraq | spin | only emulator publicly addressable | published production REST endpoint |
| Origin Wukong | Origin Quantum | gate-model SC | public REST exists but international access blocked | vendor-side international access |
| Universal Quantum Bath | Universal Quantum | trapped ion | no production endpoint yet | Universal Quantum publishes submission URL |
| AQT Marmot | AQT | trapped ion preview | preview only, paid contract access | AQT opens public preview |
| IQM Sirius | IQM | gate-model SC | research-class | upstream QPU spec page |
| Google Sycamore / Willow | Google | gate-model SC | not user-addressable; research-only | Google opens a public submission target |

## See also

- [FUTUREPLAN.md][future] — for *why* certain modalities cannot fit
  into Photon / Phonon / Spinor and need a sibling DSL.
- [Credentials guide](../operations/credentials.md) — how to
  set up live mode for the four Step-2 vendors once their endpoints
  open.
- [Chip registry source][chips] — every YAML the compiler reads.

[future]: https://github.com/nimesh08/quantum-stack/blob/main/FUTUREPLAN.md
