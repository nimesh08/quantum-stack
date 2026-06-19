# `target` — file declaration  *[directive]*

The first non-comment line of every Spinor file. Picks one of two
**contracts** for the rest of the file.

## Synopsis

```
target generic
target <chip_id>
```

## Parameters

| name | constraint |
|---|---|
| chip_id | identifier; must match a YAML in `spinor/registry/chips/<id>.yaml`, or be `generic` |

## Semantics

| Form | Contract | Gate vocabulary | Coupling |
|---|---|---|---|
| `target generic` | portable | standard gates only — `h x y z s sdg t tdg rx ry rz cx cz swap` | all-to-all |
| `target <chip>` | hardware | only `chip.native_gates` | physical wiring of `chip.coupling_map` |

Humans write `target generic`. The compiler **produces** the chip
form via the placement → routing → decomposition pipeline.

## Legality

- The line must be the first non-blank, non-comment line.
- An unknown `<chip_id>` is a parse-time error: `unknown chip id: <name>`.
- A `target generic` file that uses a [native_gate](../grammar.md) (e.g.
  `ecr`) is a [W5](../rules/W5_portable_contract.md) error.
- A `target <chip>` file that uses a non-native gate is a
  [W6](../rules/W6_hardware_contract.md) error.

## Examples

```spinor
; portable, what humans write
target generic
qubit q[2]
h q[0]
cx q[0], q[1]
```

```spinor
; hardware, what spinorc produces for IBM
target ibm_heron_r2
qubit q[2]
ecr q[17], q[18]
```

```spinor
; ERROR: must come first
qubit q[2]      ; <- this comes before `target`
target generic  ; <- too late
```

## Equivalents

- **Phonon**: `target` works the same way.
- **Photon**: optional at file scope; defaults to `generic` if omitted.

## See also

- [Grammar](../grammar.md) — `target_decl` rule
- [W5 portable contract](../rules/W5_portable_contract.md)
- [W6 hardware contract](../rules/W6_hardware_contract.md)
- [Add a chip](../../../sdks/python/cookbook.md)
