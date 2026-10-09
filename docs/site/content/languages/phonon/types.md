# Phonon types

| Type | Meaning |
|---|---|
| `qubit` | A linear quantum wire; distinct handles cannot clone its state |
| `bit` | A mutable measured-bit register |
| `bool` | An immutable controller snapshot or Boolean expression value |
| `int` | Exact signed 64-bit compile-time integer, or a saved measured bit (0/1) |
| `uint[W]` | An explicit unsigned controller value of width 1 through 64 |
| `angle` | A finite compile-time rotation parameter |

```phonon
qubit q[1]
bit c[1]
c[0] = measure q[0]
bool saved = c[0]
uint[8] count = uint[8](saved)
reset q[0]
c[0] = measure q[0]
output count
```

The later write to `c[0]` cannot change `saved`. Source assignments create new
SSA values; joins select only the taken arm. UInt arithmetic wraps modulo its
width. Comparisons yield bool; widths change only through explicit casts.
Runtime arithmetic on a plain `int` snapshot requires an explicit UInt cast.

Quantum helper returns transfer states under the existing input/output
convention; they do not copy qubits. Runtime floating-point values, dynamic
qubit indices and general classical helper-call returns remain unsupported.

See [def](reference/def.md), [return](reference/return.md), and the
[finite controller language](../../../../language/controller.md).
