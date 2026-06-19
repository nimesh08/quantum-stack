# FAQ

Short answers to questions we get often.

## Languages

**Q: Photon, Phonon, Spinor — which one do I use?**

Use **Photon** (`@photon.kernel` in Python or `.pho` files) if you
want to write algorithms — most users. Use **Phonon** if you need
linear-type guarantees, custom optimizer passes, or want to read/write
the IR directly. Use **Spinor** to inspect the chip-locked output of
the compiler or to hand-write tiny test programs.

**Q: Why three?**

Each has a different audience:

- Photon for application developers (objects, library)
- Phonon for compiler authors (IR, linear types, optimizer)
- Spinor for hardware folks (one line per gate, two contracts)

The convergence test enforces that they all produce identical Spinor
where they overlap.

## Compiler

**Q: Do I need LLVM/MLIR to build the engine?**

No. The C++ engine has an in-tree fallback that compiles without it.
LLVM/MLIR 22.1.8 enables the MLIR-backed bridge — same output, just
faster on big circuits.

**Q: Why is the optimizer in Phonon, not Spinor?**

Spinor specializes per-chip; if the optimizer ran there it would re-run
for every target, multiplying cost. Phonon runs once, above all chips,
so optimizations benefit every target automatically.

**Q: Can I add a new chip?**

Yes — by writing a single YAML under `spinor/registry/chips/<id>.yaml`.
No compiler change. See
[`spinor/registry/chips/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/registry/chips)
for examples.

## Platform

**Q: Do I need the API to run programs?**

No, only if you want multi-user, history, cost control, or a
browser playground. For "compile and look at output" use `spinorc`
or the `photon` Python package directly. See
[the three scenarios](#).

**Q: Native systemd or `heisenberg run`?**

For production: native **systemd** units, see
[Operations / Native systemd](https://nimesh08.github.io/heisenberg-platform/operations/native_systemd/).
For laptops: **`heisenberg run`** is the single command. Docker is gone.
See [Operations / Install](https://nimesh08.github.io/heisenberg-platform/operations/install/).

**Q: Why public repo?**

GitHub Pages on the free plan only works with public repos. The
sources are open architecture (the deep-dives are shareable); secrets
are env-driven and never committed.

## Submission

**Q: What does "verbatim" mean?**

The submission adapters send the OpenQASM 3 we produce **without**
letting the provider's transpiler rewrite it. `skip_transpilation=True`
for IBM, `Program(source=qasm)` for Braket, raw `openqasm-v3` for
Azure. This is Rule 5 — the only way our result quality is provably
ours.

**Q: How do I add a new provider?**

For new chips on existing providers: write the chip YAML.
For an entirely new provider (e.g. a hypothetical "PsiCorp"): add a
new `spinor_submit._live_psicorp` adapter and a calibration provider
under `platform/calibration/src/calibration/providers/psicorp.py`.

## Cost control

**Q: How is dollar cost computed?**

`shots × chip.pricing.per_shot_usd`, computed by
[`jobsvc.cost.dollar_cost`](reference/python/index.md#jobsvccost).
Compared against `Budget.daily_usd - recent_spend_today` and the
monthly window before any job is queued.

**Q: What counts as "recent spend"?**

Jobs in **Queued, Running, Completed, Failed** states. **Rejected**
jobs do not count — they never spent anything.

## Calibration

**Q: When does calibration refresh?**

02:00 UTC every night, by default. Override via
`/etc/heisenberg/heisenberg.env` or
`calibration --cron-hour H --cron-minute M`.

**Q: What if a provider call fails?**

The previous file is preserved (`os.replace` is atomic only after
the new file is fully written). The `calibration_snapshots` table
records `ok=false` with the error.

## Errors

**Q: "cannot duplicate qubit value"**

You wrote `r[0] = q[0]` somewhere. Photon/Phonon's linear-type
checker rejects clones. See [Linear types](languages/phonon/linear_types.md).

**Q: "W3: distinct operands"**

You wrote `cx q[0], q[0]` (or similar). Two-qubit gates need two
distinct qubits. The no-cloning law surfaced as a syntax check. See
[W3](languages/spinor/rules/W3_distinct_operands.md).

**Q: HTTP 402 "exceeds_daily_budget"**

You're over the daily window. Either reduce shots, pick a cheaper
chip (e.g. `generic` for free local sim), or raise the budget via
`PATCH /me/budget`.

## Where to ask

- File a GitHub issue: <https://github.com/nimesh08/quantum-stack/issues>
- Read the [Glossary](internals/glossary.md) for terms
- Browse the [build journal](internals/build_journal.md) to see what changed when
