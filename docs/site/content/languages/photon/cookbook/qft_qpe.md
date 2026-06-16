# QFT and phase estimation

## What it does

Quantum Phase Estimation: given a unitary `U` with eigenstate `|ψ⟩`
of eigenvalue `e^(2πiφ)`, estimate `φ` to `n` bits using `n+1`
qubits.

## Recipe (sketch)

```photon
target generic

def controlled_U_pow(QReg q, int control_idx, int power) {
    // Apply U^(2^power) controlled on q[control_idx].
    // For demo we use a single-qubit Z phase: U = e^(i*pi/4).
    for k in 0..power {
        q.cz(control_idx, q.size() - 1)
    }
}

kernel qpe(int n) -> int {
    QReg q(5)                // 4 estimation qubits + 1 eigenstate
    // Prepare the eigenstate of U on q[4]:
    q.x(4)                   // |1⟩ is an eigenstate of Z

    // Hadamard the estimation register
    for i in 0..4 {
        q.h(i)
    }
    // Apply controlled-U^(2^k)
    for i in 0..4 {
        controlled_U_pow(q, i, i)
    }
    // Inverse QFT on the estimation register
    q.iqft()
    return q.measure_int()
}
```

The measured `int`, divided by `2^n`, approximates `φ`.

## Why it works

Phase kickback: the controlled-U^(2^k) imprints a phase on the
control qubit proportional to the eigenvalue. The IQFT extracts the
phase as a binary number.

## See also

Library: [`qft`](../reference/library/qft.md), [`iqft`](../reference/library/iqft.md)
