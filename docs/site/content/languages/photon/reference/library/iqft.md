# `q.iqft()` — Inverse QFT  *[photon.lib]*

Apply the inverse of [`qft`](qft.md). Useful for phase estimation
and Shor's algorithm.

## Synopsis

```photon
q.iqft()
```

## What it expands to

The reverse of `qft`: SWAPs first, then unwound H + controlled-RZ
layers in reverse order with negated angles. Source:
[`photon/lang/cpp/lib/Library.cpp` `expandIqft`](https://github.com/nimesh08/quantum-stack/blob/main/photon/lang/cpp/lib/Library.cpp#L136-L152).

## Examples

```photon
kernel phase_estimate() -> int {
    QReg q(4)
    q.h(0)
    q.h(1)
    q.h(2)
    q.h(3)
    // ... apply controlled-U^(2^k) ...
    q.iqft()
    return q.measure_int()
}
```

## See also

[`qft`](qft.md), [Cookbook: qft_qpe](../../cookbook/qft_qpe.md)
