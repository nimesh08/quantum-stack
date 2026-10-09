# Photon controller values

These constructs belong to the `.pho` surface language. `int` and `angle` retain
their compile-time meanings. Use `Bit` (also `bool`) for saved Boolean values and
`UInt<N>` for controller integers, with an explicit width from 1 through 64.
Quantum indices and gate angles must still resolve at compile time.

```text
target generic
kernel example() {
  QReg q(1)
  Bit measured[1]
  q.x(0)
  measured[0] = q.measure(0)
  Bit saved = measured[0]
  q.reset(0)
  measured[0] = q.measure(0)
  UInt<8> count = UInt<8>(saved)
  count = count + 1
  output saved
  output count
}
```

`saved` keeps the first measurement's value after the destination is overwritten.
Unsigned addition/subtraction wrap modulo the declared width. Operators include
`+`, `-`, `&`, `|`, `^`, `~`, constant shifts and comparisons. Boolean arithmetic
requires an explicit unsigned cast; convert an integer to Boolean with an
explicit comparison. Width changes use `UInt<N>(value)`. Mixed widths, invalid
shift counts and out-of-range literals are errors. Precedence, from strongest
to weakest, is unary, multiplication/division for static expressions,
addition/subtraction, shifts, `&`, `^`, `|`, comparisons. Parentheses are supported.

```text
target generic
kernel retry() {
  QReg q(1)
  Bit done = false
  UInt<8> attempts = 0
  while (!done) max_iterations 3 {
    q.reset(0)
    q.h(0)
    done = q.measure(0)
    attempts = attempts + 1
  }
  output attempts
  output done
}
```

`bounded while` is an equivalent spelling. The compiler emits bounded, guarded
on-device operations and an explicit `loop_exhausted_N` application output; it
does not evaluate measurement outcomes on the host. `break` ends the innermost
loop without reporting exhaustion. `continue` skips the remaining iteration.
Conditional returns suppress following operations on that path. A value return
inside a bounded loop requires an explicit fallback return for the exhaustion
path, with matching fixed output types/widths. Measurement-valued return paths
normalize equal-width registers to one fixed classical destination register.

Branch-local quantum registers have finite static capacity and local names.
`discard q` or `discard q[i]` explicitly resets and releases ownership; later
allocation can reuse the reserved slot with another reset. Using the old binding
is an error. Conditional discard of an outer-scope register is rejected because
it would leave incompatible ownership at the branch join. Ordinary local name
expiration does not imply discard or an automatic physical reset.

These features compile only when both the concrete device and the chosen output
format explicitly admit their individual controller operations and integer
widths. A provider's generic feed-forward flag does not authorize arithmetic.
Local simulator or offline verification evidence is separate from provider
acceptance and hardware execution.
