# Phase C — M2: photon.lib

## 1. Goal & spec section

Ship `photon.lib`, the standard algorithm library: callables that
take a `QReg` (and any extra parameters) and apply the right
sequence of Phonon ops to it. Each routine is implemented as an
**in-process expander** invoked by the Photon lowerer when it
encounters a `lib.<name>(args)` call. No new compilation engine;
just a fixed catalogue of templates.

Spec section: Photon & Frontends Deep-Dive **Part 1 §2.2**
(`photon.lib`).

## 2. Inputs / outputs

**Input.** A Photon `LibCall` statement
(`q.<routine>(args)` where `<routine>` is one of the library
names) or a free-standing `lib.<routine>(q, args)` call.

**Output.** Inline Phonon-builder calls applied to the right
slots of `q`. The lowering emits the same Phonon ops it would for
hand-written equivalents, so the Phonon optimizer and Spinor
pipeline see no difference.

**Catalogue (M2 scope).**

| Routine | Signature | Lowers to |
| --- | --- | --- |
| `bell_pair(q, a, b)` | qreg slots `a`, `b` | `h q[a]; cx q[a], q[b]` |
| `ghz(q)` | qreg of size N | `h q[0]; cx q[0],q[1]; cx q[1],q[2]; …` |
| `qft(q)` | qreg of size N | bit-reversal swaps + cascading `h` and controlled-rz |
| `iqft(q)` | qreg of size N | the inverse: cascading controlled-rz† and `h` then bit-reversal |
| `grover(q, oracle, rounds)` | qreg + oracle + iteration count | superposition + (`oracle` + diffusion) × rounds |
| `teleport(q, src, anc, dst)` | three-slot teleportation | the canonical teleportation circuit |
| `vqe_ansatz(q, depth)` | hardware-efficient ansatz | depth × (per-qubit ry + chained cx) |

For M2 the **oracle** is treated as an `oracle: Oracle` parameter
the kernel receives; the expander emits a `phonon.call "oracle"`
with the QReg slots as operands. The platform layer can later
inline the oracle's body; from M2's perspective, the call site is
the contract.

## 3. Deliverables

- `photon/lang/include/photon/lang/Library.h` — public
  `expandLibrary(name, recv, args, slots, builder, loc) -> bool`.
- `photon/lang/cpp/lib/Library.cpp` — the catalogue.
- `photon/lang/cpp/lib/Lower.cpp` — calls `expandLibrary` from the
  `LibCall` case.
- `photon/tests/m2/{bell_pair_test,ghz_test,qft_test,grover_test,
  teleport_test,vqe_test}.cpp` + `CMakeLists.txt`.
- `photon/tests/corpus/lib_*.pho` — small driver kernels.

## 4. Data structures

A single internal struct passed to expanders:

```cpp
struct ExpandCtx {
  std::unordered_map<std::string, std::vector<phonon::dialect::ValueId>>* qslots;
  phonon::dialect::Builder* builder;
  phonon::dialect::Location loc;
  std::function<std::optional<int64_t>(const ExprPtr&)> foldInt;
  std::function<std::optional<double>(const ExprPtr&)> foldReal;
};
```

## 5. Algorithms

- **bell_pair(a, b):** trivial — `h(a); cx(a, b)`.
- **ghz(q[0..N-1]):** `h(q[0]); for i in 0..N-1: cx(q[i], q[i+1])`.
- **qft(q[0..N-1]):**

  ```
  for j in 0..N:
    h q[j]
    for k in j+1 .. N:
      Rz(angle = pi / 2^(k-j)) controlled on q[k] applied to q[j]
        // implemented as: cz-like with rz(angle/2) sandwich.
  for i in 0 .. N/2:
    swap q[i], q[N-1-i]
  ```

  Reference: Nielsen & Chuang §5.1, eq. (5.4).
- **iqft:** the same pattern, run in reverse, with negated angles.
- **grover(q, oracle, rounds):**

  ```
  for i in 0..N:
    h q[i]
  for r in 0..rounds:
    oracle(q)
    diffusion(q)
  ```

  where `diffusion(q)` is `H^N · X^N · multi-controlled-Z · X^N · H^N`.
  Reference: Grover, arXiv:quant-ph/9605043; multi-controlled-Z
  decomposed via Toffoli ladders for now (M5+ may swap in
  tweedledum's synthesis).
- **teleport(q, src, anc, dst):** `h(anc); cx(anc, dst); cx(src, anc);
  h(src); measure(src) -> c0; measure(anc) -> c1; if (c1 == 1) x(dst);
  if (c0 == 1) z(dst);`. The same circuit shipped in Phonon's
  teleportation corpus; M2 just packages it.
- **vqe_ansatz(q, depth):** `for d in 0..depth { for i in 0..N { ry(theta_d_i, q[i]) } for i in 0..N-1 { cx(q[i], q[i+1]) } }`.
  M2 lowers the parameter angles as `ConstAngle(0.1 * (d*N + i))`
  placeholders so the optimizer sees real constants; the Phase D
  variational driver will replace them with optimised values.

## 6. Test matrix

| ID | Type | Inputs | Expected | Gate |
| --- | --- | --- | --- | --- |
| M2.bell_pair | I | `lib.bell_pair(q, 0, 1)` | exactly 1 H + 1 CX | M2 |
| M2.ghz_n3 | I | `lib.ghz(q)` on 3-qubit reg | 1 H + 2 CX | M2 |
| M2.ghz_n5 | I | `lib.ghz(q)` on 5-qubit reg | 1 H + 4 CX | M2 |
| M2.qft_n3 | I | `lib.qft(q)` n=3 | op-count matches catalogue | M2 |
| M2.qft_n4 | I | `lib.qft(q)` n=4 | op-count matches catalogue | M2 |
| M2.grover_rounds3 | I | `lib.grover(q, oracle, 3)` | 3 oracle + 3 diffusion blocks | M2 |
| M2.teleport | I | `lib.teleport(q, 0, 1, 2)` | matches Phonon teleportation corpus shape | M2 |
| M2.vqe_depth2 | I | `lib.vqe_ansatz(q, 2)` | depth-2 ansatz emitted | M2 |
| M2.unknown_lib | U | `lib.nonsense(...)` | precise error message | M2 |
| M2.regression.pipeline | E | bell.pho through full Phonon pipeline | typechecks + optimizes + lowers | M2 |

## 7. Cookbook example

A small `lib_ghz.pho`:

```
target generic
kernel ghz3() -> int {
  QReg q(3)
  q.ghz()           // photon.lib expansion
  return q.measure_int()
}
```

After lowering: 3 alloc_qubit + 3 alloc_bit + 1 H + 2 CX + 3
measure + return, all wrapped in phonon.def/end_def.

## 8. Pitfalls

- **QFT angle precision.** `pi / 2^k` underflows for large k; the
  expander caps depth at 30 and warns above that. Caught by
  `M2.qft_n4` (sanity).
- **Multi-controlled-Z.** A naive ladder grows exponentially; M2
  uses Toffoli decomposition. Caught by `M2.grover_rounds3` op
  counts.
- **Loop-bound folding.** `lib.qft(q)` reads `q`'s size — needs
  the slot table at expansion time. Caught by `M2.qft_n3` /
  `M2.qft_n4`.
- **`lib.<name>` collision with user identifiers.** Names are
  reserved at the lib level — a user method named the same is
  diagnosed at parse time.
- **Free-form vs method form.** Both `lib.ghz(q)` and `q.ghz()`
  resolve to the same expander. Caught by `M2.ghz_n3`.

## 9. Definition of Done

- [ ] All M2 tests pass.
- [ ] `lib.<name>` calls produce the same Phonon op-count as
      hand-written equivalents.
- [ ] Phonon's typechecker, optimizer, and Spinor lowering accept
      every M2-produced module.
- [ ] `phaseC/test-matrix.md` updated.
- [ ] `phaseC_progress.md` appended.

## 10. Open questions

- The grover oracle is left as a `phonon.call "oracle" args`. We
  could instead require the oracle to be inlined at parse time;
  for M2 the call-site contract is enough. **Resolution: defer to
  the Phase D platform driver.**
