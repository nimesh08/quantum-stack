# `q.ghz()` — n-qubit GHZ state  *[photon.lib]*

Build the n-qubit GHZ state $(|0...0\rangle + |1...1\rangle)/\sqrt{2}$ across the
**entire** register.

## Synopsis

```photon
q.ghz()
```

No parameters. Spans `q[0]..q[N-1]`.

## What it expands to

```
q.h(0)
for i in 0..(N-1) {
    q.cx(i, i+1)
}
```

Source:
[`photon/lang/cpp/lib/Library.cpp` `expandGhz`](https://github.com/nimesh08/quantum-stack/blob/main/photon/lang/cpp/lib/Library.cpp#L105-L115).

## Examples

```photon
kernel ghz5() -> int {
    QReg q(5)
    q.ghz()                  ; spans all 5 qubits
    return q.measure_int()   ; 00000 or 11111, never anything else
}
```

## See also

[`bell_pair`](bell_pair.md), [Spinor cookbook: ghz](../../../spinor/cookbook/ghz.md)
