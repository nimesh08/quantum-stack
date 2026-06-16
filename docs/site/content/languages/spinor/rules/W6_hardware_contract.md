# W6 — hardware contract

Under `target <chip>`:

- Only the chip's `native_gates` are legal.
- Every two-qubit gate must sit on a connected edge of `chip.coupling_map`.
- Qubit indices are **physical** positions on the silicon.

## Why

Hardware-locked code is what gets shipped to the provider. The
verifier ensures we never emit something the chip cannot execute.

## Diagnostic

Two flavours:

```
error: W6: gate 'h' is not native to ibm_heron_r2; native_gates: ecr rz sx x
```

```
error: W6: cx q[0], q[5] — qubits 0 and 5 are not adjacent on heavy_hex_156
```

## Examples

```spinor
target ibm_heron_r2
qubit q[2]
cx q[0], q[1]                ; ERROR W6: cx not native; use ecr
```

```spinor
target ibm_heron_r2
qubit q[156]
ecr q[0], q[100]             ; ERROR W6: not connected on heavy-hex
```

## Fix

In normal usage you never write a hardware-contract `.spn` by hand —
the compiler produces it from `target generic`. If you do (e.g. to
inspect the routed output), use the chip's native gates and check
`coupling_map` for connectivity.

## See also

[W5 portable contract](W5_portable_contract.md), [`target`](../reference/target.md),
[`ecr`](../reference/ecr.md), [Add a chip](../../../tutorial/add_a_chip.md)
