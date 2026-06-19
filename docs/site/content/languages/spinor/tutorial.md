# Spinor — tutorial

Your first complete program in Spinor: a Bell pair, written by hand
and compiled to two different chips. By the end of this page you
will have run a circuit through every layer of the compiler.

## What we are building

A Bell state is the smallest non-trivial entangled program. We
prepare two qubits, apply a Hadamard to one and a controlled-X
between them, then measure both. The histogram should be roughly
half `00` and half `11` — never `01` or `10`. That correlation is
the signature of entanglement.

## Step 1 — write the source

Save this as `bell.spn`:

```spinor
; A Bell pair: |Phi+> = (|00> + |11>) / sqrt(2).
target generic
qubit q[2]
bit c[2]

h q[0]                      ; superposition on q[0]
cx q[0], q[1]               ; entangle q[1] with q[0]
c = measure q               ; collapse both qubits into c[0], c[1]
```

Six lines. The first declares the **portable contract** (`target
generic`). The next two declare two qubits and two classical bits.
The last three are the program proper.

## Step 2 — verify it

```bash
spinorc verify -t generic bell.spn
```

The verifier runs seven static checks (declare-before-use, indices
in range, distinct operands on multi-qubit gates, ...). On success
it prints nothing and exits 0. Try breaking the program — change
`cx q[0], q[1]` to `cx q[0], q[0]` — and watch the verifier reject
it: *"distinct operands required"*.

## Step 3 — compile to a real chip

```bash
spinorc compile -t ibm_heron_r2 bell.spn
```

This runs the full pipeline:

1. **Placement** assigns the logical qubits `q[0]` and `q[1]` to a
   pair of physical qubits on Heron r2's heavy-hex topology.
2. **Routing** — they are already adjacent, so no SWAPs are needed.
3. **Decomposition** rewrites `H q[0]; CX q[0], q[1]` into Heron's
   native gate set: `RZ`, `SX`, `ECR`. The `CX` becomes
   `H q[1]; ECR q[0], q[1]; H q[1]` plus the standard ECR-to-CNOT
   identity.
4. **Cleanup** drops trivial identities.
5. The output is printed in chip-locked Spinor syntax.

Now compile to an IonQ chip:

```bash
spinorc compile -t ionq_forte bell.spn
```

The same six lines lower to a different gate set — `MS`, `GPI`,
`GPI2` — and an all-to-all coupling. Same source, two physical
realisations.

## Step 4 — emit to a wire format

```bash
spinorc emit -t ibm_heron_r2 -f qasm3 --verbatim bell.spn
```

You get OpenQASM 3.1 with a `#pragma braket verbatim` envelope and
physical qubits referenced as `$0`, `$1`. This is what the
provider layer hands to IBM Quantum runtime.

## Step 5 — run it

The full end-to-end run, in cassette mode (no cloud account
needed):

```bash
spinorc submit -t ibm_heron_r2 --provider ibm --mode cassette \
  --shots 1000 \
  /tmp/bell.qasm3 \
  > histogram.json
cat histogram.json
```

In live mode (`--mode live`) the same command sends the artefact
verbatim to IBM Quantum runtime, polls until completion, and prints
the histogram.

## What you have learned

- Spinor is one line per machine operation; no loops, no functions.
- The first line picks a contract. `target generic` is portable;
  `target <chip-id>` is hardware.
- The compile pipeline is **place → route → decompose → cleanup →
  emit**. Each stage is in
  [`spinor/passes/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/passes).
- The same source compiles to multiple chips; the chip YAMLs in
  [`spinor/registry/chips/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/registry/chips)
  are the only thing that changes.

## Where to next

- More recipes: the [Cookbook](cookbook/index.md).
- Formal grammar: [Lexical](lexical.md), [Grammar](grammar.md),
  [Types](types.md).
- Move up a layer:
  [Phonon](../phonon/index.md) lets you write the same program with
  loops and conditionals.

---

!!! quote "Credits"
    **Spinor** was designed and implemented by **Nimesh Cheedella**.
