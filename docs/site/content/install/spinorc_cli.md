# `spinorc` CLI

The Phase A command-line driver. Compiles `.spn` source through the full
Spinor pipeline and emits whatever you ask for.

## Install

`spinorc` is built by the top-level CMake build. Follow
[Install from source](from_source.md), then it's at:

```
build/spinor/cli/spinorc
```

Add it to PATH or alias:

```bash
alias spinorc="$PWD/build/spinor/cli/spinorc"
export SPINOR_REGISTRY_ROOT="$PWD/spinor/registry"
```

`SPINOR_REGISTRY_ROOT` tells the loader where to find chip YAMLs.
Default is `./spinor/registry/` relative to the current working dir.

## Subcommands

```
spinorc parse    <FILE.spn>
spinorc verify   -t <chip> <FILE.spn>
spinorc compile  -t <chip> <FILE.spn>
spinorc check    -t <chip> <FILE.spn>
spinorc emit     -t <chip> -f <qasm3|qir|quil> [--verbatim] <FILE.spn>
spinorc registry list
```

### `parse`

Reads the file and prints the resulting IR. Useful for debugging
grammar issues; equivalent to running just the parser.

### `verify`

Runs the W1–W6 + type-check verifier against the supplied target.
Errors are printed with file/line/column.

### `compile`

The full `M3 + M5 + M6` pipeline (verify → place → route → decompose →
cleanup). Output is the chip-locked Spinor IR.

### `check`

Runs the M8 check lane: compiles AND simulates both the input and
output programs to assert equivalence (up to global phase). Prints a
resource summary.

```
$ spinorc check -t ionq_aria_proto bell.spn
equivalence: skipped (chip has 25 qubits; statevector capped at 12)
gates total:        10
gates two-qubit:    1
depth:              9
qubits:             25
measurements:       2
error (est.):       0.0188744
cost @1k shots ($): 10
```

For chips with ≤12 qubits the equivalence check runs end-to-end.

### `emit`

Compiles, then serialises to one of the three target formats.

```bash
spinorc emit -t ionq_forte -f qasm3 --verbatim bell.spn
```

`--verbatim` is honoured by the QASM3 emitter (wraps body in
`#pragma braket verbatim box { ... }` per Braket's spec).

### `registry list`

Lists every chip the registry knows about.

```
$ spinorc registry list
ibm_heron_r2
ionq_aria_proto
ionq_forte
quantinuum_helios
```

## Smoke test

```bash
echo 'target generic
qubit q[2]
h q[0]
cx q[0], q[1]
' > bell.spn

spinorc compile -t ibm_heron_r2 bell.spn
```

You should see `ecr`, `rz`, `sx` ops on physical qubits.

## See also

- [Spinor language manual](../languages/spinor/index.md) — the language
  this CLI compiles.
- [Spinor cookbook](../languages/spinor/cookbook/bell.md) — Bell, GHZ,
  reset/reuse, and more.
- [Add a chip](../tutorial/add_a_chip.md) — extend the registry.
