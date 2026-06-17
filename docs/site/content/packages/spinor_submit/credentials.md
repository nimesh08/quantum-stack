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

## QCI (Quantum Circuits Inc.)

QCI Aqumen ships through the `qci` provider, but the **production
REST endpoint and auth scheme are not yet publicly documented**.
The adapter at `_live_qci` raises a clear `RuntimeError`; only the
cassette path is wired today. To enable live mode:

1. Receive sign-up access from QCI at
   <https://quantumcircuits.com/products>.
2. Replace the body of `_live_qci` in
   [`spinor_submit/__init__.py`][spinor_submit_init] with the
   documented submit + poll pattern, citing the URL.
3. Set the credentials env var QCI publishes (placeholder:
   `QCI_API_TOKEN`).

See [Unsupported chips](../../chips_unsupported.md) for what
specifically is missing.

## Anyon Technologies

Anyon Yukon is `anyon` provider; cloud submission API is in private
beta. The adapter raises until Anyon publishes their REST URL:

```bash
# placeholder
export ANYON_API_TOKEN=<provided-by-anyon>
```

## TII (Technology Innovation Institute, Abu Dhabi)

TII does not run a hosted submission service. They publish the
open-source [Qibolab](https://github.com/qiboteam/qibolab) driver
which talks directly to lab hardware. To enable live mode, point the
adapter at a user-hosted Qibolab instance:

```bash
export QIBOLAB_PLATFORM=tii_falcon
export QIBOLAB_HOST=https://qibolab.your-lab.example
```

## Alice & Bob

Alice & Bob ship a public cat-qubit emulator (Felis); production
hardware access is via paid contract. Live mode raises until the
production submission URL is published. Placeholder env vars:

```bash
export ALICEBOB_API_KEY=<provided-by-alice-and-bob>
```

## See also

[Modes](modes.md), [Recording cassettes](recording_cassettes.md)

[spinor_submit_init]: https://github.com/nimesh08/quantum-stack/blob/main/spinor/submit/python/spinor_submit/__init__.py
