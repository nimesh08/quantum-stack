# FUTUREPLAN.md — Quantum hardware landscape and the roadmap beyond Step 2

> **Status: in progress.** Steps 1 and 2 are landed (this commit takes
> us from 4 to 26 verified chips with zero compiler change). This
> document captures the full landscape, why we shipped what we
> shipped, and what comes next, in priority order.

> **How to navigate:** every section heading is anchor-linked from the
> table of contents below. The deep-dives (Sections 5, 7, 8) carry
> grammar sketches, lowering examples, type-checker constraints, and
> effort estimates so a future contributor can pick one up and run
> with it.

---

## Table of contents

| #  | Section                                                                            | What it answers |
|----|------------------------------------------------------------------------------------|-----------------|
| 1  | [The whole landscape](#1-the-whole-landscape)                                      | Every modality, every vendor, every chip we know about (table). |
| 2  | [Why we ship 26 chips today](#2-why-we-ship-26-chips-today)                        | What "Bucket A" and "Bucket B" mean, mapped to M1 and M2. |
| 3  | [What competitors ship](#3-what-competitors-ship)                                  | Horizon Triple Alpha, NVIDIA CUDA-Q, IBM Qiskit, Google Cirq — verified upstream. |
| 4  | [Architectural support taxonomy](#4-architectural-support-taxonomy)                | Bucket A / B / C / D / E with concrete examples. |
| 5  | [Step 3 — analog Rydberg DSL deep-dive](#5-step-3-analog-rydberg-dsl-deep-dive)   | Grammar, AHS lowering, type checker, 2–3 week estimate. |
| 6  | [Step 4 — opportunistic unlocks](#6-step-4-opportunistic-unlocks)                 | D-Wave gate-mode, Microsoft Majorana, Diraq, Origin Wukong. |
| 7  | [Step 5 — photonic DSL deep-dive](#7-step-5-photonic-dsl-deep-dive)               | Modes / beam-splitters / detect grammar, Blackbird lowering, ORCA + Xanadu, 3–4 weeks. |
| 8  | [Step 6 — annealing DSL deep-dive](#8-step-6-annealing-dsl-deep-dive)             | problem / var / minimize grammar, BQM lowering, D-Wave, 2–3 weeks. |
| 9  | [Step 7 — Phase E auto-synthesis](#9-step-7-phase-e-auto-synthesis)               | Why this sits above Photon; decade-scale. |
| 10 | [Why none of these fit into Photon/Phonon/Spinor](#10-why-none-of-these-fit-into-photonphononspinor) | Concrete grammar / type / lowering arguments. |
| 11 | [Decision matrix](#11-decision-matrix)                                             | When to build each; demand triggers; cost-of-delay. |
| 12 | [Glossary](#12-glossary)                                                           | All the acronyms in one place. |
| 13 | [References](#13-references)                                                       | Every URL with the date we verified it. |

---

## 1. The whole landscape

There are five families of quantum hardware in active production or
serious R&D today. Counting "chip" loosely (a single physical device
that a user can address), the rough population looks like this
(verified upstream 2026-06-16):

| Family            | Modality                | Major vendors                                                                                  | Approx. cloud-reachable chips | Modality fits Spinor today? |
|-------------------|-------------------------|------------------------------------------------------------------------------------------------|-------------------------------|-----------------------------|
| Gate-model SC     | Superconducting transmon / fluxonium | IBM, Rigetti, IQM, OQC, Google, QCI, Anyon, TII, Origin                            | ~25                           | **Yes** (this is Spinor's home turf) |
| Gate-model TI     | Trapped ion              | IonQ, Quantinuum, AQT, Universal Quantum                                                       | ~8                            | **Yes** |
| Gate-model neutral | Rydberg gate-mode        | Atom Computing, Pasqal (gate-mode roadmap), QuEra (small)                                      | ~3                            | Partial (gate set differs; needs YAML + maybe a Phonon lowering for global pulses) |
| Analog neutral    | Rydberg analog           | QuEra, Pasqal, Infleqtion (preview)                                                            | ~3                            | **No** — not gate-model. Needs a sibling DSL (Step 3). |
| Photonic CV       | Continuous-variable / squeezed light                | Xanadu, ORCA, PsiQuantum (roadmap)                                                             | ~3                            | **No** — modes, not qubits. Needs photonic DSL (Step 5). |
| Annealing         | Ising / QUBO             | D-Wave (Advantage 6.4, Advantage2)                                                             | ~2                            | **No** — solves an objective, not a circuit. Needs annealing DSL (Step 6). |
| Topological       | Majorana zero modes      | Microsoft (Majorana 1)                                                                         | 0 publicly reachable          | TBD (Microsoft's gate set is gate-model; once a real device ships, Bucket B). |
| Other             | Spin (Diraq), cat-qubit (Alice & Bob) | Diraq, Alice & Bob, SiQure                                                                     | ~2                            | Mostly **Yes** with a custom gate set. |

The gap between "we ship 26" and "we could ship ~40" is mostly
adapter work for the few remaining gate-model vendors and the
compiler-recipe gaps tracked in
[`docs/site/content/chips_unsupported.md`](docs/site/content/chips_unsupported.md).
Everything off the gate-model column needs a sibling DSL — see
section 10 for *why*.

---

## 2. Why we ship 26 chips today

Two buckets of work:

- **Bucket A — YAML only.** Add a chip to the registry by writing one
  YAML file. The compiler reads it. No code change. This is what
  Phase A's whole architecture was built to enable. The 18 chips in
  M1 (IBM Heron r3 / Brisbane / Sherbrooke / Osprey / Nighthawk r1 /
  Torrino, Quantinuum H2-1 / H1-1, IonQ Tempo / Forte Enterprise /
  Aria-1 / Harmony, Rigetti Ankaa-3 / Ankaa-2 / Ankaa-9Q-3, IQM
  Garnet / Emerald, OQC Toshiko, AQT Pine) are Bucket A.

- **Bucket B — adapter.** Add a `_live_<vendor>` dispatch entry in
  [`spinor_submit/__init__.py`](spinor/submit/python/spinor_submit/__init__.py) so jobs route to the right
  cloud. The 4 vendors in M2 (QCI Aqumen, Anyon Yukon, TII Falcon,
  Alice & Bob Boson 4) are Bucket B.

This is the point of Spinor. The compiler does *not* need to know
about a new chip's name, only its native gate set, topology, and
decomposition recipes. The chips that we still cannot ship (despite
having the YAML written) are blocked on either:

1. **A compiler recipe gap.** The current KAK two-qubit decomposer
   ships recipes for `ecr`, `ms`, and `rzz` entanglers. CZ-native and
   CX-native chips (most of Rigetti, all of IQM and OQC, the four
   Step-2 vendors, IBM Heron r3 and IBM Nighthawk r1) error out at
   decomposition time with `emitCX: no recipe for entangler 'cz'`.
   Adding the recipes is a Phase A code change (~1 week). Tracked
   in [`chips_unsupported.md`](docs/site/content/chips_unsupported.md).

2. **A vendor with no public production endpoint.** QCI, Anyon, TII,
   and Alice & Bob all have public datasheets but no published REST
   submission URL. Their adapters ship in cassette mode only and the
   live-mode body raises `RuntimeError` pointing at
   `chips_unsupported.md`. The fix is one PR per vendor once the URL
   is published.

---

## 3. What competitors ship

| Stack | Chips supported | Modalities | Source | Verified |
|-------|----------------|-----------|--------|----------|
| **Heisenberg** (this repo) | 26 (after M1 + M2) | Gate-model SC + TI + 4 cassette-only | this commit | 2026-06-16 |
| Horizon Quantum **Triple Alpha** | ~12, IBM-only focus | Gate-model SC | <https://horizonquantum.com/triple-alpha> | 2026-06-16 |
| NVIDIA **CUDA-Q** | ~25, multi-vendor; deep IBM + IonQ + Quantinuum + ORCA + Anyon support | Gate-model SC + TI + photonic | <https://nvidia.github.io/cuda-quantum/latest/using/backends/backends.html> | 2026-06-16 |
| **Qiskit** | IBM-only (~10 backends) | Gate-model SC | <https://docs.quantum.ibm.com/run/get-backend-information> | 2026-06-16 |
| **Cirq + cirq-rigetti / cirq-ionq** | ~6 multi-vendor | Gate-model SC + TI | <https://quantumai.google/cirq> | 2026-06-16 |
| **PennyLane** plugin ecosystem | wide via plugins (qiskit, braket, ionq, ibm, …) | Gate-model + photonic (strawberryfields) | <https://pennylane.ai/plugins/> | 2026-06-16 |

The story we tell is: *Heisenberg is the only stack with verbatim
pass-through to the vendor (Rule 5) plus cost control driven by a
real `ResourceEstimate` plus nightly calibration refresh, on a
cleanly-modelled MLIR-style IR (Phonon)*. CUDA-Q is the closest
competitor; we differ on the verbatim guarantee.

---

## 4. Architectural support taxonomy

Five buckets, in order of incremental cost:

| Bucket | What it is                                            | Cost (1 chip) | Examples (this repo) |
|--------|-------------------------------------------------------|---------------|----------------------|
| **A**  | Add a YAML in `spinor/registry/chips/`; compiler reads it. No code. | ~30 min       | M1's 18 chips. |
| **B**  | A new submission adapter (`_live_<vendor>`)           | ~1 day        | M2's 4 vendors. |
| **C**  | Vendor-blocked: chip exists publicly but the cloud submission URL is not yet public. | indefinite    | QCI, Anyon, TII, Alice & Bob's *live* mode. |
| **D**  | Modality does not fit gate-model semantics; needs a sibling DSL with its own grammar / types / lowering. | weeks         | Rydberg analog (Step 3); photonic CV (Step 5); annealing (Step 6). |
| **E**  | Auto-synthesis: the user describes a problem, the system *invents* the circuit. Sits **above** Photon. | months / years | Phase E (Step 7). |

The buckets are mutually exclusive. A new chip only ever sits in one.


---

## 5. Step 3 — analog Rydberg DSL deep-dive

**Vendors targeted:** QuEra Aquila, Pasqal Fresnel, Infleqtion (preview).
**Effort estimate:** 2–3 weeks.
**Status:** designed, not started.

### Why it cannot be Spinor

Analog Rydberg machines do **not run gates**. The user specifies an
array of neutral atoms in 2D positions, then drives them with a
time-dependent Hamiltonian over a schedule. The fundamental object
is `(atoms, Ω(t), Δ(t))`, not `(qubits, gates)`. Spinor's parser
demands `target / qubit / gate-mnemonic`; an attempt to express
`Ω(t)` in Spinor would have to encode the whole Hamiltonian as a
gigantic Trotterised circuit, defeating the point.

### Sketch grammar (proposed name `Photic`)

```
program       ::= 'target' chip-id atom-array schedule
atom-array    ::= 'atoms' '{' ATOM ( ',' ATOM )* '}'
ATOM          ::= 'a' '[' INT ']' '@' '(' FLOAT ',' FLOAT ')'    ; positions in micrometres
schedule      ::= 'schedule' '{' segment+ '}'
segment       ::= duration_us 'omega' rabi-trace ('detune' detune-trace)?
rabi-trace    ::= 'linear' '(' f0 ',' f1 ')' | 'piecewise' '[' fs ']'
detune-trace  ::= 'linear' '(' d0 ',' d1 ')' | 'piecewise' '[' ds ']'
```

### Sample program

```rydberg
target quera_aquila

atoms {
  a[0] @ (0.0, 0.0)
  a[1] @ (4.0, 0.0)
  a[2] @ (8.0, 0.0)
}

schedule {
  4.0us  omega linear(0, 15.0e6)  detune linear(-15.0e6, 0)
  16.0us omega linear(15.0e6, 0)  detune linear(0, 15.0e6)
}
```

### Lowering

The target is **AHS (Analog Hamiltonian Simulation) JSON**, AWS
Braket's wire format for analog Rydberg jobs:
<https://docs.aws.amazon.com/braket/latest/developerguide/braket-analog-ahs.html>.
Pasqal accepts a similar `Pulser` JSON. The mapping is direct:

- `atoms` → AHS `register.sites` + `filling`.
- `omega` schedule → AHS `hamiltonian.drivingField.amplitude`.
- `detune` schedule → AHS `hamiltonian.drivingField.detuning`.

### Type-checker constraints

- `atoms` count ≤ chip max (Aquila: 256; Pasqal Fresnel: 100).
- All atom positions must be reachable by the SLM (chip-specific
  rectangle, e.g. Aquila: 75 µm × 75 µm).
- Minimum inter-atom spacing ≥ chip-specific Rydberg-blockade radius
  (Aquila: 4.0 µm).
- `omega` ≥ 0, `omega` ≤ chip Rabi cap (Aquila: 15.8 MHz).
- Total schedule duration ≤ chip coherence cap (Aquila: 4 µs).

### Effort breakdown

| Sub-task | Days |
|---|---|
| Lexer + parser (~600 LOC C++)            | 3 |
| AST + verifier (W1–W6 analogues)         | 2 |
| AHS lowerer + JSON emit                  | 3 |
| Pasqal Pulser-JSON lowerer               | 2 |
| Registry entries (3 chips)               | 0.5 |
| Submission adapter through Braket        | 1 |
| Tests (parse, verify, lower, round-trip) | 2 |
| Docs                                     | 1.5 |

---

## 6. Step 4 — opportunistic unlocks

These are gate-model targets we *could* slot into Bucket A or B once
upstream publishes the missing piece. None requires a new DSL.

| Vendor | Device | Bucket | Blocker | What unlocks it |
|--------|--------|--------|---------|-----------------|
| D-Wave | "Advantage 2" gate-mode preview | A | Gate-model preview is in private trial; the YAML is a 1-day write. | D-Wave publishes the native gate set + native two-qubit gate. |
| Microsoft | Majorana 1 | A | Device not user-reachable; gate set is published in research. | Microsoft opens the Azure target. |
| Diraq | 8-spin prototype | A | Vendor only ships an emulator today. | Diraq publishes the production endpoint. |
| Origin Quantum | Wukong (72-qubit SC) | B | Public REST exists but auth is China-only. | Vendor-side international access. |
| Universal Quantum | Bath-cooled trap (preview) | A | No production endpoint yet. | Submission URL public. |

Cost: ~1 day each, total ≤ 1 week to absorb everything in this row.
Triggered opportunistically — wait for upstream and ship.

---

## 7. Step 5 — photonic DSL deep-dive

**Vendors targeted:** Xanadu Borealis / X-series, ORCA PT-1 series,
PsiQuantum (roadmap).
**Effort estimate:** 3–4 weeks.
**Status:** designed, not started.

### Why it cannot be Spinor

Photonic CV (continuous-variable) computers operate on **modes** and
**Gaussian states**, not qubits. The fundamental gates are
beam-splitters (`BSgate(θ, φ)`), squeezing (`Sgate(r)`), and
displacement (`Dgate(α)`). State is described by a Gaussian
distribution (mean + covariance), not a complex amplitude vector.
Spinor's whole IR — `qubit q[N]; bit c[N]; gate q;` — is incompatible.
Even discrete-variable photonic platforms (ORCA's time-bin photons)
require a `detect` primitive that has no qubit equivalent.

### Sketch grammar

```
program     ::= 'target' chip-id mode-decl op* detect
mode-decl   ::= 'modes' '[' INT ']'
op          ::= 'sgate' INT FLOAT
              | 'dgate' INT '(' FLOAT ',' FLOAT ')'
              | 'bs'    INT INT FLOAT FLOAT
              | 'mzgate' INT INT FLOAT FLOAT
              | 'measure_homodyne' INT FLOAT
              | 'measure_pnr' INT
detect      ::= 'detect' '{' DETECTOR ( ',' DETECTOR )* '}'
DETECTOR    ::= 'pnr' INT '->' BIT | 'homodyne' INT FLOAT '->' FLOAT
```

### Sample program (GBS instance)

```photonic
target xanadu_borealis

modes [216]
sgate 0   1.4
sgate 1   1.4
bs 0 1    0.7853981   0
bs 1 2    0.7853981   0
detect { pnr 0 -> b[0], pnr 1 -> b[1] }
```

### Lowering

- **Xanadu**: emit **Blackbird** (Strawberry Fields' wire format).
  Blackbird is YAML-shaped DSL plus a JSON variant; the Strawberry
  Fields runtime accepts either.
  <https://strawberryfields.ai/photonics/conventions/blackbird.html>.
- **ORCA**: emit ORCA's **TBI (Time-Bin Interferometer) JSON**;
  ORCA's PT-1 series exposes a REST API that takes a list of
  beam-splitter angles plus a detection schedule.

### Type-checker constraints

- Modes count must equal chip's `modes` field.
- Squeezing parameters bounded by chip-specific cap (Borealis: r ≤ 1.4).
- BS angles in `[0, π/2]`.
- PNR-detector count ≤ chip detector count.

### Why squeezing cannot be a "gate" in Spinor

Squeezing is **not unitary on a finite-dimensional Hilbert space** —
it's unitary on an infinite-dimensional Fock space. Spinor's
`gate` op is hard-typed to `qubit`s with a finite-dim matrix. The
verifier W4 (gate matrix is unitary on `2^n × 2^n`) is exactly the
rule that rejects an attempt to lift `Sgate(r)` into Spinor.

---

## 8. Step 6 — annealing DSL deep-dive

**Vendors targeted:** D-Wave Advantage 6.4, Advantage2.
**Effort estimate:** 2–3 weeks.
**Status:** designed, not started.

### Why it cannot be Spinor

Annealing solves an objective, not a circuit. The user *describes*
an Ising / QUBO problem and asks for a low-energy solution; the
hardware doesn't run a sequence of unitaries the user wrote. This
is genuinely a different computational model.

### Sketch grammar (proposed name `Anneal`)

```
program       ::= 'target' chip-id problem
problem       ::= 'problem' '{' var-decl* objective minimisation chain* '}'
var-decl      ::= 'var' IDENT 'in' '{' INT ',' INT '}'
objective     ::= 'minimize' qubo-expr
qubo-expr     ::= term ( '+' term )*
term          ::= COEF '*' IDENT ( '*' IDENT )?
chain         ::= 'chain' '[' IDENT ( ',' IDENT )* ']' 'strength' COEF
```

### Sample program

```anneal
target dwave_advantage2

problem {
  var s0 in {-1, 1}
  var s1 in {-1, 1}
  var s2 in {-1, 1}

  minimize -1 * s0 * s1 + 0.5 * s1 * s2 + 0.2 * s0
}
```

### Lowering

Lower to **BQM (Binary Quadratic Model)** in D-Wave's `dimod` JSON
shape, then submit through D-Wave's `dwave-system`.
<https://docs.ocean.dwavesys.com/projects/dimod/en/latest/reference/sampler_composites/index.html>.

The compiler does:

1. Linear coefficients → `bqm.linear`.
2. Quadratic coefficients → `bqm.quadratic`.
3. Variable type (`spin` ∈ {-1, 1} vs `binary` ∈ {0, 1}) → BQM
   `vartype`.
4. Embedding onto the Pegasus graph (Advantage2) is delegated to
   D-Wave's `EmbeddingComposite`; we don't reimplement minor-embedding.

### Type-checker constraints

- Variable count ≤ chip logical qubit budget post-embedding.
- Coefficients within chip range (Advantage2: ±2 for h, ±1 for J).
- Chains must be subsets of declared variables.

### Why this cannot be a Phonon optimisation pass

Phonon's optimiser rewrites a sequence of `gate` ops while
preserving unitary equivalence (verified by Phonon's W-rules). The
input to D-Wave is *not* a gate sequence — it is a static description
of an objective function. There's nothing to "optimise" in Phonon's
sense. It's a different IR entirely.


---

## 9. Step 7 — Phase E auto-synthesis

**Status:** out of scope per Rule 6. Decade-scale.

Auto-synthesis is the dream of: *user describes the problem in
natural language or mathematical specification, the system invents
a circuit that solves it, picks the best target, schedules the
shots, and aggregates the results.* This is **not** a compiler — it's
a planner / search problem that *uses* a compiler. The right
architectural picture is:

```
problem spec   --->   AUTO-SYNTHESISER (Phase E)   --->   Photon source
                            |                                  |
                       chooses target                       calls Phonon → Spinor
                       chooses ansatz
                       chooses optimiser
```

Auto-synthesis sits **above** Photon, not inside it. Photon is the
spec-language for circuits; Phase E is the spec-language for
*problems*. They share no grammar.

Concrete things Phase E would have to do that no current system does
end-to-end:

- Map a SAT / TSP / MaxCut / chemistry Hamiltonian to a parametrised
  ansatz (QAOA, VQE, Trotterised UCC).
- Pick a chip from the registry that fits qubit count + coherence.
- Orchestrate variational training (classical optimiser-in-the-loop).
- Aggregate noisy shots into a confidence-bounded answer.

Each of those is a research summit on its own. The right move is to
treat Phase E as a **standalone product** built on top of the
finished compiler stack, not as a Phase D extension.

---

## 10. Why none of these fit into Photon/Phonon/Spinor

A consolidated argument, family by family. Each claim cites the
exact part of the existing stack the family would break.

### Analog Rydberg

- **Spinor breaks:** `Lexer.cpp:13-19` defines a closed gate
  vocabulary. There is no token for `omega(t)`. Adding it would
  also force us to invent a time-dependent Hamiltonian sub-language
  inside Spinor, doubling the surface area.
- **Phonon breaks:** Phonon's IR is `Op = Gate q | Measure q | If c
  then ... else ...`. There is no IR node for "drive a Hamiltonian
  for 4 µs". The optimiser's invariants (cancellation, rotation
  merging) all assume gate ops.
- **Photon breaks:** the OO `QReg` API has methods like
  `qreg.h(0); qreg.cx(0,1)` — none of which apply when the
  fundamental object is `(atoms, schedule)`.

### Photonic CV

- **Spinor's W4 (unitary gate matrix on `2^n × 2^n`) rejects
  `Sgate(r)`** — it is unitary on the infinite-dim Fock space, not
  on `C^{2^n}`. The verifier *correctly* fails.
- **Type system breaks:** Phonon's linear types track `qubit`, not
  `mode`. A `mode` is a different resource (carries Gaussian state,
  gets squeezed, gets photon-number-resolved, not measured-into-bit).
  Conflating the two would gut the no-cloning theorem.

### Annealing

- **Compilation pipeline irrelevant:** Spinor's whole purpose is to
  place + route + decompose a circuit onto a topology. An annealing
  job has no circuit, no placement, no routing.
- **Optimiser irrelevant:** Phonon rewrites gate sequences. There
  is no gate sequence.
- **Conclusion:** sharing a frontend with Photon would be
  syntactic-only and would mislead users into thinking annealing
  jobs go through the same compiler. Separate language is clearer.

### Photonic discrete (ORCA)

- Slightly closer to gate-model — beam-splitters look like
  two-qubit gates — but ORCA's `detect` primitive returns a *photon
  count*, not a bit. Phonon's IR has no "count" node.

### Topological (Microsoft Majorana)

- This one *is* gate-model. Once Microsoft opens a public target,
  it slots straight into Bucket A. Listed for completeness.

---

## 11. Decision matrix

| Step | Action                          | Trigger to start                                                | Effort     | Risk  | Cost-of-delay |
|------|---------------------------------|-----------------------------------------------------------------|------------|-------|---------------|
| 1    | 18 gate-model YAMLs             | **DONE** in this commit                                         | ~1 day     | low   | — |
| 2    | 4 Step-2 vendor adapters        | **DONE** in this commit                                         | ~1 day     | low   | — |
| 3    | Analog Rydberg DSL              | First customer who asks for Aquila / Fresnel                    | 2–3 weeks  | med   | High — Pasqal + QuEra are closing the gap fast in 2026 |
| 4    | Opportunistic gate-model unlocks | Vendor publishes endpoint                                       | 1 day each | low   | Medium — Origin / D-Wave gate-mode demand growing |
| 5    | Photonic CV DSL                 | First customer who asks for Borealis / X-series                 | 3–4 weeks  | high  | Low — small market |
| 6    | Annealing DSL                   | First operations-research customer with a QUBO workload         | 2–3 weeks  | low   | Medium — D-Wave is mature |
| 7    | Phase E auto-synthesis          | After Steps 3–6 stabilise and we have one bona-fide ML customer | months     | very high | — |

The honest priorities are: Step 3 (Rydberg) > Step 4 (opportunistic)
> Step 6 (annealing) > Step 5 (photonic) > Step 7 (Phase E).

---

## 12. Glossary

- **AHS** — Analog Hamiltonian Simulation. AWS Braket's wire format
  for analog Rydberg jobs.
- **BQM** — Binary Quadratic Model. D-Wave's standard problem
  representation.
- **Bucket A / B / C / D / E** — see Section 4.
- **Cassette** — a recorded provider response, used by tests so
  CI doesn't need real cloud tokens.
- **CV photonic** — Continuous-variable photonic computing. Modes,
  not qubits.
- **DSL** — Domain-Specific Language.
- **Fock space** — infinite-dim Hilbert space of photon number
  states.
- **GBS** — Gaussian Boson Sampling. Photonic computing primitive.
- **KAK** — Cartan / KAK decomposition. The recipe Spinor's
  decomposer uses for two-qubit gates.
- **PNR** — Photon-Number Resolving (detector).
- **QCCD** — Quantum Charge-Coupled Device. Quantinuum's trapped-ion
  architecture.
- **Rydberg blockade** — physical effect that makes a pair of nearby
  excited atoms into an effective gate.
- **Triple Alpha** — Horizon Quantum's compiler product.
- **W1–W7** — Spinor's verifier rules. Each rejects an
  ill-formed program with a precise diagnostic.

---

## 13. References

All URLs verified upstream **2026-06-16**.

- IBM Quantum chip catalog: <https://quantum.cloud.ibm.com/computers>
- IBM Nighthawk announcement: <https://www.ibm.com/quantum/blog/nighthawk>
- IBM 2033 roadmap: <https://www.ibm.com/quantum/blog/quantum-roadmap-2033>
- AWS Braket IonQ: <https://aws.amazon.com/braket/quantum-computers/ionq/>
- AWS Braket Rigetti: <https://aws.amazon.com/braket/quantum-computers/rigetti/>
- AWS Braket IQM: <https://aws.amazon.com/braket/quantum-computers/iqm/>
- AWS Braket OQC: <https://aws.amazon.com/braket/quantum-computers/oqc/>
- AWS Braket QuEra (Aquila / AHS): <https://docs.aws.amazon.com/braket/latest/developerguide/braket-analog-ahs.html>
- Azure Quantinuum: <https://learn.microsoft.com/azure/quantum/provider-quantinuum>
- AQT product page: <https://www.aqt.eu/products/>
- IQM Emerald product page: <https://www.meetiqm.com/products/iqm-emerald>
- QuEra Aquila: <https://www.quera.com/aquila>
- Pasqal Fresnel: <https://www.pasqal.com/products/fresnel/>
- Xanadu Borealis: <https://www.xanadu.ai/products/borealis/>
- Strawberry Fields Blackbird format: <https://strawberryfields.ai/photonics/conventions/blackbird.html>
- ORCA Computing PT-1: <https://orcacomputing.com/products/>
- D-Wave Ocean dimod (BQM): <https://docs.ocean.dwavesys.com/projects/dimod/>
- Quantum Circuits Inc. products: <https://quantumcircuits.com/products>
- Anyon Technologies Yukon: <https://anyontech.com/yukon/>
- Qibolab (TII): <https://github.com/qiboteam/qibolab>
- Alice & Bob Cloud: <https://alice-bob.com/products/cloud/>
- NVIDIA CUDA-Q backend list: <https://nvidia.github.io/cuda-quantum/latest/using/backends/backends.html>
- Horizon Triple Alpha: <https://horizonquantum.com/triple-alpha>
- Qiskit backend reference: <https://docs.quantum.ibm.com/run/get-backend-information>
- Cirq: <https://quantumai.google/cirq>
- PennyLane plugins: <https://pennylane.ai/plugins/>

---

*End of FUTUREPLAN.md. Mirror at
[`docs/site/content/futureplan.md`](docs/site/content/futureplan.md);
the canonical copy is here at the repo root.*
