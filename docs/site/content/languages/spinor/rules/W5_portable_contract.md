# W5 — portable contract

Under `target generic`:

- Only **standard** gates are legal: `h x y z s sdg t tdg rx ry rz cx cz swap`.
- Native gates (`ecr ms rzz sx sxdg gpi gpi2 u1q`) are **not** legal.
- Any qubit may interact with any qubit (all-to-all coupling).
- Qubit indices are **virtual** labels.

## Why

`target generic` is the **input** to the compiler. Native gates and
physical wiring constraints are introduced by the compiler's
placement / routing / decomposition passes. A portable program
shouldn't bake in chip-specific assumptions.

## Diagnostic

```
error: W5: native gate 'ecr' is not legal under 'target generic'
```

## Examples

```spinor
target generic
qubit q[2]
ecr q[0], q[1]               ; ERROR W5
```

```spinor
target generic
qubit q[2]
cx q[0], q[1]                ; OK — standard gate
```

## Fix

Use the standard equivalent (e.g. `cx` instead of `ecr`), or switch
to a hardware contract (`target ibm_heron_r2`) if you really mean to
write chip-specific code.

## See also

[W6 hardware contract](W6_hardware_contract.md), [`target`](../reference/target.md)
