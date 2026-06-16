# Variational loop — `vqe_ansatz` + outer optimizer

## What it does

VQE (Variational Quantum Eigensolver) — minimize ⟨ψ(θ)|H|ψ(θ)⟩ where
|ψ(θ)⟩ comes from a parameterised ansatz.

## Recipe (Photon kernel)

```photon
target generic

kernel vqe(int depth) -> int {
    QReg q(4)
    q.vqe_ansatz(depth)      // 4-qubit, depth-layer ansatz with baked-in angles
    return q.measure_int()
}
```

## Recipe (Python driver)

```python
import photon
import scipy.optimize as opt

def evaluate(angles):
    """Compile the kernel with these angles, submit shots, return ⟨H⟩."""
    @photon.kernel
    def k():
        q = photon.QReg(4)
        # ... apply rotations from the `angles` array ...
        # ... apply the Hamiltonian's Pauli decomposition ...
        return q.measure_int()
    hist = k.run(shots=1000, target="ibm_heron_r2")
    return compute_expectation(hist, hamiltonian)

x0 = [0.1] * 4
result = opt.minimize(evaluate, x0, method='COBYLA')
```

## Why it works

Each compilation freezes the angles into a circuit; the outer
optimizer (COBYLA, SPSA, Adam) walks the parameter space using the
quantum hardware's energy estimates as the objective.

## Cost

depth × (4 single + 3 CX) per compile. Plus the cost of N evaluations
× shots per evaluation. Realistic VQE runs are 1000-10000 compiles,
each at 1000-10000 shots — significant, hence the cost-control seam.

## Variations

- The current `vqe_ansatz` has **fixed angles**; for a real VQE you
  build your own parameterised circuit (per-layer `q.ry(theta_i, j)`).
  A future Photon `vqe_ansatz(depth, angles[])` is on the roadmap.

## See also

Library: [`vqe_ansatz`](../reference/library/vqe_ansatz.md), Phonon: [parameterized_circuit](../../phonon/cookbook/parameterized_circuit.md)
