# Parameterised gates

## What it does

Spinor's continuous gates (`rx`, `ry`, `rz`, `rzz`) take an angle
parameter. Four shapes are accepted; you cannot do general expressions.

## Recipe

```spinor
target generic
qubit q[1]
rz(pi)        q[0]           ; literal pi
rz(pi/2)      q[0]           ; pi divided by an integer
rz(0.7853981) q[0]           ; raw radians
rz(2*pi)      q[0]           ; real multiplied by pi
rz(-pi/4)     q[0]           ; unary minus on any of the above
```

## Why it works

The `angle` grammar rule:

```ebnf
angle ::= real | [ "-" ] "pi" [ "/" integer ] | real "*" "pi"
```

The lexer recognises `pi`; the parser folds the four shapes into a
double. There is no general expression evaluator.

## Variations

- **VQE-style parameter sweep**: write `rx(theta) ...` is **not**
  legal in Spinor. Spinor doesn't have variables. For parameter
  sweeps, generate one `.spn` file per parameter value, or use
  Phonon (which supports `angle` parameters in `def`s).
- **Combine rotations**: `rz(pi/4) rz(pi/4) ≡ rz(pi/2)`. The optimizer
  merges these.

## Same in Phonon

Same syntax. Phonon adds parametric `def`s:

```phonon
def parameterized_x(qubit qq, angle theta) {
    rx(theta) qq
}
```

## Same in Photon

```photon
QReg q(1)
q.rz(pi/2, 0)
```

## Side effects on cost

`rz` on chips with virtual rz: free at runtime. Other rotations
count as one single-qubit gate each.

## Where to look

- Reference: [`rx`](../reference/rx.md), [`ry`](../reference/ry.md), [`rz`](../reference/rz.md)
- Grammar: [`angle` rule](../grammar.md)
