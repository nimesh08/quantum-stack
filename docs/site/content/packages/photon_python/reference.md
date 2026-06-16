# Reference — `photon` Python package

## Public surface

| Name | Kind | Purpose |
|---|---|---|
| `photon.kernel` | decorator | mark a function as a quantum kernel |
| `photon.QReg(n)` | helper class | declare a quantum register |
| `photon.compile_phonon(text, target)` | function | direct engine call |
| `photon.UnsupportedConstructError` | exception | raised by the AST translator |
| `photon.CompilationError` | exception | raised by the engine |
| `photon.__version__` | str | package version |

## Signatures

```python
def kernel(fn: Callable) -> KernelObject: ...

class KernelObject:
    phonon_text: str
    compiled: photon._engine.CompiledProgram
    target: str
    def run(self, *, shots: int = 1000, target: str | None = None) -> dict[str, int]: ...

class QReg:
    def __init__(self, n: int): ...
    def h(self, i: int) -> None: ...
    def x(self, i: int) -> None: ...
    # ... every Spinor gate ...
    def cx(self, c: int, t: int) -> None: ...
    def measure_int(self) -> int: ...

def compile_phonon(text: str, target: str = "generic") -> CompiledProgram: ...
```

For the engine binding's `CompiledProgram` and `ResourceEstimate`,
see [`photon/bindings/include/photon/bindings/Engine.h`](https://github.com/nimesh08/quantum-stack/blob/main/photon/bindings/include/photon/bindings/Engine.h).

## See also

- [Decorator internals](kernel_decorator.md)
- [Photon language manual](../../languages/photon/index.md)
