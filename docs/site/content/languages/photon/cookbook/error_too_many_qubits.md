# Error: too many qubits

## What it does

Demonstrates what happens when you try to compile a kernel that needs
more qubits than the chip has.

## Recipe (broken)

```photon
target ionq_aria_proto       ; 25 qubits

kernel oversized() -> int {
    QReg q(50)               ; 50 > 25
    q.ghz()
    return q.measure_int()
}
```

## What you'll see

```
error: kernel 'oversized' allocates 50 qubits but target
       'ionq_aria_proto' has only 25 qubits
help: pick a larger chip (ibm_heron_r2 has 156 qubits) or
       reduce the QReg size
   --> oversized.pho:3:5
    |
  3 |     QReg q(50)
    |     ^^^^^^^^^^
```

## Fix

Pick a bigger chip:

```photon
target ibm_heron_r2          ; 156 qubits — plenty
```

…or split the algorithm across multiple smaller circuits and combine
results classically.

## See also

[`QReg`](../reference/QReg.md), [Add a chip](https://nimesh08.github.io/heisenberg-platform/sdks/python/cookbook/)
