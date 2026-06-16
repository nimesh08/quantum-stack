# Grover with a custom oracle

## What it does

Find a "needle" basis state in a Grover-style search, with a
user-defined oracle that marks the needle.

## Recipe

```photon
target generic

def needle_oracle(QReg q) {
    // Mark |1010⟩ as the needle: phase-flip iff bits are exactly 1010.
    q.x(0)                   // the 0 bits
    q.x(2)
    // Multi-controlled Z on (q0, q1, q2, q3) with q3 as target.
    // For 4 qubits we use a Toffoli decomposition.
    q.h(3)
    // ... (decomposed multi-CZ; truncated for brevity)
    q.h(3)
    q.x(0)
    q.x(2)
}

kernel search() -> int {
    QReg q(4)
    q.grover(needle_oracle, 3)   // 3 rounds is optimal for 16 basis states
    return q.measure_int()
}
```

The result should peak at `0b1010 = 10` with high probability.

## Why it works

The oracle phase-flips the needle. Grover's diffusion operator
amplifies the marked amplitude. After ~⌊π/4·√N⌋ rounds the needle
dominates.

## Cost

4 qubits, 3 rounds × (oracle + diffusion). Oracle uses ~6 single +
1 multi-CZ. Diffusion is H^4 X^4 multi-CZ X^4 H^4. Total ~50 gates,
~10 two-qubit gates.

## See also

Library: [`grover`](../reference/library/grover.md)
