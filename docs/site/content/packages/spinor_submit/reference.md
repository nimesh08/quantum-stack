# Reference — `spinor_submit`

## Public surface

| Name | Kind | Purpose |
|---|---|---|
| `spinor_submit.submit(qasm, chip, *, provider, shots, program_name)` | function | submit verbatim |
| `spinor_submit.Histogram(counts, shots)` | dataclass | result |
| `spinor_submit.Job(id, status, provider)` | dataclass | job handle |
| `spinor_submit.SUPPORTED_PROVIDERS` | tuple | `("ibm", "aws", "azure", "local")` |

## Signatures

```python
@dataclass
class Histogram:
    counts: dict[str, int] = field(default_factory=dict)
    shots: int = 0

@dataclass
class Job:
    id: str
    status: str           # "queued" | "running" | "completed" | "error"
    provider: str

def submit(
    qasm_text: str,
    chip: str,
    *,
    provider: str,        # "ibm" | "aws" | "azure" | "local"
    shots: int = 1000,
    program_name: str = "default",
) -> Histogram: ...
```

## See also

[Modes](modes.md), [Credentials](credentials.md), [Cookbook](cookbook.md)
