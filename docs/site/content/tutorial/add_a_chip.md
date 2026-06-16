# Add a chip in 30 minutes

The deepest claim of the architecture: **adding a new device costs us a
YAML file, not compiler code.** Here we'll prove it by adding a
fictional all-to-all 16-qubit chip called `acme_q1`.

## 1. Write the YAML

Save as
[`spinor/registry/chips/acme_q1.yaml`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/registry/chips):

```yaml
id: acme_q1
provider: local
qubits: 16
native_gates: [ecr, rz, sx, x]
coupling_map:
  topology: all_to_all
  size: 16

supports:
  mid_circuit_measure: true
  feedforward: limited
  reset: true

calibration:
  source: fixture
  refresh: never
  store: ~/.cache/spinor/calibration/acme_q1.json

decomposition:
  one_qubit:
    recipe: euler_zyz
    rotation_gate: rz
    pi_2_gate: sx
  two_qubit:
    recipe: kak
    entangler: ecr
    entangler_count_max: 3

pricing:
  per_shot_usd: 0.005

notes: |
  Demo chip used to test the YAML-only-new-chip property.
```

## 2. Restart the API

The registry is loaded once per process. In compose:

```bash
docker compose --project-directory ../.. -f docker-compose.yml restart jobsvc worker
```

(Or in dev: just restart `uvicorn`.)

## 3. Confirm the API sees it

```bash
curl -s http://localhost:8000/api/v1/targets | jq '.[] | select(.id=="acme_q1")'
```

```json
{
  "id": "acme_q1",
  "provider": "local",
  "qubits": 16,
  "native_gates": ["ecr", "rz", "sx", "x"],
  "coupling_topology": "all_to_all",
  "supports": {
    "mid_circuit_measure": true,
    "feedforward": "limited",
    "reset": true
  },
  "pricing_per_shot_usd": 0.005
}
```

## 4. Submit a Bell to it

In the playground: pick `acme_q1` from the target dropdown.

Or via API:

```bash
curl -s -X POST http://localhost:8000/api/v1/jobs \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{
        "source": "target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n",
        "source_kind": "spinor",
        "target": "acme_q1",
        "shots": 100
      }' | jq .estimate
```

```json
{ "num_qubits": 2, "depth": 4, "two_qubit_count": 1, "t_count": 0 }
```

The compiler placed, routed, decomposed, and emitted for `acme_q1`
without any code change. The cost was $0.005 × 100 = $0.50.

## What just happened

The YAML feeds four things at compile time:

| Field | Used by |
|---|---|
| `coupling_map` | placement + routing (SABRE) |
| `native_gates` + `decomposition` | gate decomposition |
| `supports.*` | legalisation (mid-circuit measure, feedforward) |
| `calibration.store` | nightly refresh writes here; placement reads it |
| `pricing.per_shot_usd` | jobsvc cost control |

`provider: local` routes execution to our in-process simulator. To
add a chip backed by IBM, AWS, Azure, or a custom provider, set
`provider: ibm | aws | azure | <your-provider>` and ensure the
matching adapter is registered (see [Add a calibration provider](add_a_provider.md)).

## Where to next

- The full registry schema is documented at
  [`spinor/docs/registry-schema.md`](https://github.com/nimesh08/quantum-stack/blob/main/docs/build/phaseA/registry-schema.md).
- [Python: `jobsvc.registry`](../api/python/jobsvc/registry.md) — how the
  service reads chip YAMLs.
- [Add a calibration provider](add_a_provider.md) — wiring nightly
  data fetches for your new chip.
