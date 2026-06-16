# `q.qft()` — Quantum Fourier Transform  *[photon.lib]*

Apply the QFT to the entire register. The bit-reversed permutation is
included via final SWAPs.

## Synopsis

```photon
q.qft()
```

No parameters. Operates on `q[0]..q[N-1]`.

## What it expands to

```
for j in 0..N {
    q.h(j)
    for k in (j+1)..N {
        q.crz(pi/2^(k-j), k, j)   // controlled-RZ
    }
}
for i in 0..N/2 {
    q.swap(i, N-1-i)              // bit reversal
}
```

The controlled-RZ is implemented via the Nielsen & Chuang Ex 4.34
identity. Source:
[`photon/lang/cpp/lib/Library.cpp` `expandQft`](https://github.com/nimesh08/quantum-stack/blob/main/photon/lang/cpp/lib/Library.cpp#L118-L134).

## Examples

```photon
kernel demo_qft() -> int {
    QReg q(4)
    // ...prepare some state...
    q.qft()
    return q.measure_int()
}
```

## See also

[`iqft`](iqft.md), [Cookbook: qft_qpe](../../cookbook/qft_qpe.md)
