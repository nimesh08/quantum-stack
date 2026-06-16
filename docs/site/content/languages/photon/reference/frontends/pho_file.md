# `.pho` source files  *[frontend]*

The canonical text form of a Photon program. What `photon::lang::parse`
reads.

## File anatomy

```photon
target generic               ; optional

def helper(QReg q, int i) {
    q.h(i)
}

kernel main() -> int {
    QReg q(2)
    helper(q, 0)
    q.cx(0, 1)
    return q.measure_int()
}
```

- One optional [`target`](../target.md) line.
- Zero or more [`def`](../../../phonon/reference/def.md) helpers.
- One or more [`kernel`](../kernel.md) entry points.

## Compile

```cpp
#include "photon/lang/Parser.h"
#include "photon/lang/Lower.h"
#include "photon/bindings/Engine.h"

auto pr = photon::lang::parse(read_file("bell.pho"), "bell.pho");
auto lr = photon::lang::lowerToPhonon(*pr.module);
auto cp = photon::bindings::CompiledProgram::fromPhononModule(
    std::move(*lr.module), "generic");
```

(Or use the platform's `POST /api/v1/jobs` with `source_kind: "photon"`.)

## Convergence

The same kernel written as `.pho`, as `@photon.kernel`, and as
`[[photon::kernel]]` produces identical Spinor — see
[three-door convergence](../../rules/three_door_convergence.md).

## See also

[`@photon.kernel`](photon_kernel.md), [`[[photon::kernel]]`](cpp_attribute.md)
