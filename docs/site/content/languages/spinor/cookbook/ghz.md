# GHZ state

## What it does

Builds the n-qubit Greenberger–Horne–Zeilinger state $(|0...0\rangle + |1...1\rangle)/\sqrt{2}$.
The n-qubit generalisation of Bell.

## Recipe (3 qubits)

```spinor
target generic
qubit q[3]
bit c[3]
h q[0]
cx q[0], q[1]
cx q[1], q[2]
c = measure q
```

## Why it works

`h q[0]` superposes; the chained `cx`s propagate the entanglement
along the qubits. The final state is $(|000\rangle + |111\rangle)/\sqrt{2}$.

## Variations

- **Star topology** (`cx q[0], q[k]` for k=1..n-1) gives the same
  state, but every cx originates from q[0]. On all-to-all chips it's
  equivalent; on heavy-hex it routes differently.
- **5-qubit GHZ**: extend the chain to `q[0]..q[4]`.
- **Cat state** is a synonym in some texts.

## Same in Phonon (with a `for` loop, n parameterised)

```phonon
target generic
qubit q[3]
bit c[3]
h q[0]
for i in 0..2 {              ; i = 0, 1
    cx q[i], q[i+1]
}
c = measure q
```

## Same in Photon

```photon
kernel ghz() -> int {
    QReg q(3)
    q.ghz()                  ; library routine; expands to h + chained cx
    return q.measure_int()
}
```

## Side effects on cost

3 qubits, 2 two-qubit gates, depth ~5. ~$0.66 on ibm_heron_r2 @ 1000 shots.

## Where to look

- Reference: [`h`](../reference/h.md), [`cx`](../reference/cx.md)
- Photon library: [`ghz`](../../photon/reference/library/ghz.md)
- Test corpus: [`spinor/tests/corpus/ghz.spn`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/tests/corpus/ghz.spn)
