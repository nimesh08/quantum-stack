# Provider credentials (live mode)

`SPINOR_SUBMIT_MODE=live` only. Cassette and local modes don't need
any of this.

## IBM Quantum

Get a token from <https://quantum.ibm.com> → Account → Token.

```bash
export IBM_QUANTUM_TOKEN=<your-token>
```

The adapter uses
`from qiskit_ibm_runtime import QiskitRuntimeService, Session, SamplerV2`.
First call uses the env token; subsequent calls reuse the cached
profile in `~/.qiskit/`.

`SamplerV2(session=session).run([qc], shots=shots, skip_transpilation=True)`
— **verbatim**, by Rule 5.

## AWS Braket

Use standard `~/.aws/credentials`. Confirm with
`aws sts get-caller-identity`. The adapter calls
`from braket.aws import AwsDevice; device.run(Program(source=qasm), shots)`.

The adapter maps chip ids to ARNs in `_live_aws`'s `arn_map`:

```python
arn_map = {
    "ionq_forte": "arn:aws:braket:us-east-1::device/qpu/ionq/Forte-1",
}
```

For new chips, extend the map at
[`spinor_submit/__init__.py`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/submit/python/spinor_submit/__init__.py).

## Azure Quantum

```bash
export AZURE_QUANTUM_RESOURCE_ID=/subscriptions/.../workspaces/...
export AZURE_QUANTUM_LOCATION=westus
```

(Or any of the other authentication mechanisms `azure-quantum`'s
`Workspace()` accepts.) The adapter submits with
`input_data_format="openqasm-v3"` — verbatim by default for Azure
QIR Adaptive.

## See also

[Modes](modes.md), [Recording cassettes](recording_cassettes.md)
