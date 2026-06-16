# Compile error: gate after measurement

## What it does

Demonstrates W4 firing.

## Recipe (broken)

```phonon
target generic               ; "generic" doesn't promise mid-circuit measure
qubit q[1]
bit  c[1]
h q[0]
c[0] = measure q[0]
h q[0]                       ; ERROR W4
```

## What you'll see

```
error: W4: gate 'h' on qubit 'q[0]' which was just measured
note: target 'generic' does not guarantee mid-circuit measurement
help: insert 'reset q[0]' if your chip supports it, or compile for
      a specific chip with supports.mid_circuit_measure: true
   --> demo.phn:6:1
    |
  6 | h q[0]
    | ^^^^^^
```

## How to fix it

Three options:

### 1. Insert `reset` and use a chip that supports it

```phonon
target ibm_heron_r2
qubit q[1]
bit  c[1]
h q[0]
c[0] = measure q[0]
reset q[0]
h q[0]                       ; OK now
```

### 2. Defer the measurement to the end

```phonon
target generic
qubit q[1]
bit  c[1]
h q[0]
h q[0]                       ; do all gates first
c[0] = measure q[0]          ; measure last
```

### 3. Use a fresh qubit

```phonon
target generic
qubit q[2]
bit  c[1]
h q[0]
c[0] = measure q[0]
h q[1]                       ; different qubit; no conflict
```

## See also

[W4 no op after measure](../../spinor/rules/W4_no_op_after_measure.md),
[`reset`](../../spinor/reference/reset.md)
