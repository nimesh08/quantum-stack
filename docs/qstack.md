# Compiler and provider submission

The 0.6 flow is Photon source → Phonon logical IR → Spinor physical IR →
versioned artifact → authenticated provider submission → persistent job results.
The C++ compiler owns decomposition, optimization, placement and routing. SDKs
construct native objects and handle authentication/transport; no SDK transpiler
is called. Required provider service translation is recorded in job receipts.

## Install and build

From a checkout:

```sh
python -m pip install -e spinor/submit/python
cmake -S . -B build -G Ninja -DQSTACK_ENABLE_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The in-tree compiler works without the optional LLVM/MLIR bridge. Select a
provider extra, for example `pip install -e 'spinor/submit/python[ibm]'` or
`[google]`, `[aws]`, `[azure]`, `[quantinuum]`, `[rigetti]`, `[iqm]`, `[oqc]`,
`[aqt]`, `[qir]`, `[qibolab]`. Direct IonQ/Anyon/Felis REST uses the standard
library; Felis/QIR compilation needs the QIR extra for binary exports.
SDK constraints are in `spinor/submit/python/pyproject.toml`.

Installed compiler binaries can be selected with `QSTACK_PHOTONC`,
`QSTACK_PHONONC`, `QSTACK_SPINORC`; otherwise qstack searches PATH and the
checkout's build directory. The C++ wrappers use `QSTACK_PYTHON` and optionally
`QSTACK_PYTHONPATH` to select the same Python environment.

## Compile and run

```text
qstack providers
qstack targets list
qstack compile examples/qstack/bell.pho --target ibm_fez -O 2 --out build/bell
qstack submit build/bell --mode local --shots 1024 --wait
qstack run examples/qstack/bell.pho --target ibm_fez --mode local --wait
```

`local` executes the real C++ state-vector/measurement simulator. `cassette`
returns explicitly labeled stored fixture data. `live` authenticates and
submits a remote job. A mode is mandatory through an argument or configuration;
there is no silent switch to cassette/local execution. `--dry-run` validates
offline without constructing a provider client. `auth check` makes authenticated
read-only requests; it does not submit a validation job.

`photonc`, `phononc`, and `spinorc` retain their compile/emit commands. Their
run/submit commands delegate to qstack, sharing the same resolver and receipts.
QASM source submission remains supported with an explicit target and validation.

The optimizer levels are O0 required lowering/legality, O1 exact local
simplification, O2 commutation/block optimization with topology routing, and
O3 bounded layout/native-resynthesis searches. Compilation is deterministic.
Approximate synthesis is not enabled; the recorded error budget is zero.
Measurement, reset and classical conditions form optimization boundaries.
Unsupported features, unbound parameters and missing native recipes are errors.
Counted loops preserve their induction step and comparison. Phonon `while`
loops are expanded only when their condition is known at compile time and they
terminate within the compiler's expansion limit. Function specialization binds
angles, register indices, allocation sizes and static loop bounds at each call.
Lexical captures and parameter shadows preserve their separate bindings;
qubit and readout arguments retain their original register slots. Expansion is
bounded to 128 nested calls and 100000 expanded calls or loop iterations.
Runtime unbounded loops and recursive calls are diagnosed explicitly.
Provider serialization can impose numeric precision limits; Google's Engine
protobuf stores numeric gate arguments as float32, which is recorded in its
job receipt. This does not enable approximate synthesis in the compiler.

Artifacts contain `manifest.json`, `physical.json`, `native.spinor`, a native
program (`.qasm3`, `.bc`, `.ll`, `.quil`, or `.json`), `target.json`,
`optimization.json`, and `mappings.json`. Content hashes detect inconsistent
files. Reports include gate/two-qubit counts, dependency depth, and a resource
schedule. Duration is reported only when every relevant instruction has timing
data. Untimed provider formats may be retimed by the service; these estimates
do not claim enforced pulse timing. Branch counts include both bodies;
depth/duration use worst-case joins. Qubit and readout mappings accompany results.

## Configuration and credentials

Precedence, highest first:

1. CLI arguments, including explicit credential arguments/file/stdin.
2. Process environment.
3. Selected dotenv files, with the last file winning.
4. Selected TOML application profile.
5. The provider SDK's documented credential/profile defaults.

`--env-file PATH` and `--env-path PATH` are aliases and repeatable. Explicit
files replace default discovery. Without them, only `.env` in the current
directory is read. `--no-env-file` disables that read; combining it with explicit
files is an error. Missing/malformed explicit files are errors. Dotenv is parsed
as data, with interpolation disabled; `${NAME}` stays literal. The resolver
never mutates the parent's environment or searches ancestor dotenv files.

```dotenv
QSTACK_IBM_API_KEY=replace-with-your-key
QSTACK_IBM_INSTANCE_CRN=replace-with-instance-crn
QSTACK_IBM_DEVICE=ibm_fez
```

```text
qstack auth check --provider ibm --env-file "C:\quantum\ibm.env"
qstack config show --resolved --provider ibm --env-path "C:\quantum\ibm.env"
qstack targets refresh --provider ibm --env-file "C:\quantum\ibm.env"
qstack compile examples/qstack/bell.pho --target ibm_fez --provider ibm --env-file "C:\quantum\ibm.env" --out build/live-bell
qstack submit build/live-bell --mode live --env-file "C:\quantum\ibm.env" --shots 1024
```

`--config PATH --profile NAME` selects application TOML; `--sdk-profile` selects
the provider's own AWS/QCS/etc profile. `auth configure` writes nonsecret
settings. `--store-key` explicitly stores supplied secrets in an OS keyring
using the optional `keyring` extra. Plaintext secrets are rejected in TOML.
Provider OAuth caches remain SDK-managed. Use `--api-key-file` or
`--api-key-stdin` when preferable to a literal `--api-key` argument. Source labels
appear in `config show --resolved`; secret values are redacted. Do not put
credentials into source code, target snapshots, or custom result payloads.
Every credential field also has matching file and stdin options, such as
`--token-file`, `--access-token-stdin` and `--client-secret-file`. Only one field
may read stdin in a command; use separate files for additional credentials.
Field definitions generate option help, environment aliases, validation and
secret classification. Later dotenv files override earlier files even when
they use different documented aliases for the same field.

Canonical variables follow `QSTACK_<ROUTE>_<FIELD>`. Existing standard AWS,
Azure and Google variables and SDK-specific aliases remain accepted. The
complete generated option list is available with `qstack --help`.

## Access routes and native formats

There are 12 cloud routes. AWS/Azure are access routes and use their own
credentials; separate direct hardware-vendor credentials are not needed.

| Route | Selection and authentication | Program/processing |
|---|---|---|
| IBM | API key, instance CRN, concrete backend | Owned native QuantumCircuit → Runtime SamplerV2; QASM3 export |
| Google | ADC, project, discovered processor, approved access | Native Cirq moments → Engine program protobuf |
| Quantinuum | Nexus browser login/SDK refresh, region, project, device | Verified QIR bitcode via qnexus |
| Azure | Azure identity/workspace resource ID or enabled connection string, exact target | Target-declared QIR, Quantinuum QASM2, or IonQ native JSON |
| AWS | AWS profile/SSO/IAM/access keys, region, real ARN, S3 location | Native Braket OpenQASM3 verbatim |
| IonQ | API key, backend ID | Native versioned REST circuit JSON; backend-specific entanglers/units |
| Rigetti | QCS OAuth/profile, processor ID | Native Quil; mandatory QCS control translation |
| IQM | Server URL, bearer token, computer selection | Native instructions checked against architecture |
| OQC | QCaaS URL, access token, exact QPU ID | QASM/QIR; mandatory QAT processing; see capability attestation below |
| AQT | Arnica browser/client credentials/token, workspace/resource | Native R/RZ/RXX/measurement via transpiler bypass |
| Anyon | Host, username, access token, project, machine | Native JSON matching Snowflurry contract |
| Alice & Bob | Felis API key, target ID | Target-restricted QIR; cat-qubit limits enforced |

The [provider contract reference](../spinor/submit/python/qstack/providers/CONTRACTS.md)
links official API/SDK sources, exact assumptions and optional capabilities.
`auth login` uses browser/session flows where advertised; token-only providers
use configure/check. An interactive MFA challenge cannot be satisfied by an
unattended invocation; login interactively first or use a documented machine
identity. Do not repeatedly retry a rejected login.

OQC's SDK exposes account QPU discovery but does not standardize the calibration
JSON topology contract. For live compilation, `--capability-snapshot PATH`
supplies an account-provided capability contract bound to the exact QPU and
current calibration digest. See the provider reference for its schema. The CLI
rejects unverified/changed contracts rather than inventing topology.

IQM architectures with computational resonators use owned MOVE routing. The
compiler derives computational interactions from calibrated, ordered MOVE/CZ
loci, preserves reserved resonator slots, and emits balanced MOVE/CZ/MOVE
sequences. Local simulation checks the allowed MOVE subspace and returns the
resonators to their empty state. No SDK routing or transpilation is invoked.

Aqumen/QCI and TII Falcon cloud submission remain unavailable until their
vendor contracts are supplied. The optional `qibolab` route accepts a configured
platform with `--qibolab-platform NAME_OR_DIRECTORY` (`QIBOLAB_PLATFORM` is also
accepted). Its built-in assembler uses the platform's calibrated native pulse
definitions, resource layers and acquisition-to-classical mappings. An installed
`--qibolab-bridge module:factory` remains available for custom laboratory setups.
Dummy controllers cannot authorize live submission. This route executes real
configured instruments and is not a Falcon cloud service. OQC Lucy is not
advertised as an AWS device.

## Targets and persistent jobs

All 31 historical YAML profiles are preserved across 11 vendors. They are
offline references, not evidence of current cloud availability. Readiness is
explicit: needs-refresh, family alias, restricted, offline-only, retired, or
unavailable. `targets refresh` caches an authenticated device snapshot separately.
An IBM family alias needs `--device` identifying a concrete discovered device.
Google targets exist only through authorized processor discovery.

The historical IBM topology files are synthetic offline fixtures, not device
wiring. They remain labelled for reproducible old examples; authenticated IBM
discovery supplies the actual per-instruction directed connectivity for live
compilation. Historical pricing and calibration notes are not current estimates.

Submission refreshes current capabilities and validates native operations,
directed coupling, per-gate loci, capacity, control flow, units and physical
labels. Incompatible changes require refreshing and recompiling; there is no
fallback to another device. The artifact records the snapshot used to compile.

Submission returns a persistent `JobReceipt`; `--wait` also waits and returns
the result. `QSTACK_STATE_DIR` can select a state directory for target snapshots
and credential-free receipts/results. Otherwise the OS user-state directory is
used. Receipts preserve route/device, workspace/account selection, SDK profile,
artifact hash, readout mapping and execution mode for restart/retrieval.

```text
qstack jobs status PROVIDER_JOB_ID
qstack jobs results PROVIDER_JOB_ID --env-file "C:\quantum\ibm.env"
qstack jobs cancel PROVIDER_JOB_ID
```

Cancellation/status/discovery are advertised only when the SDK contract exposes
them. Raw provider results are retained. Counts are normalized only from actual
integer samples/histograms with known ordering; probabilities remain raw.
Transient reads use bounded backoff. Creation requests are never automatically
retried after an ambiguous failure, to avoid duplicate jobs. Use provider job
history for reconciliation before retrying submission.

`--cost-cap-usd` rejects an estimate above the cap and rejects any route whose
USD cost cannot be evaluated. An absent estimate is never treated as zero.
`qstack estimate ARTIFACT` returns provider-backed pricing where supported.
Provider quotas, subscriptions and account permissions remain external.

## Readiness and migration

Validation levels are separate: **contract-tested** means offline request,
serialization and response tests; **account-access verified** requires the
user's authenticated read-only check; **hardware-executed** requires a configured
live smoke test. Offline CI never claims the latter two.

For 0.5 callers: explicitly choose a mode; consume typed artifacts/receipts;
use `--wait` when a synchronous result is required. The `spinor_submit` Python
module/command remains a compatibility facade, and Python Photon `.run()` now
uses actual compiler/simulator results. Unsupported Python/Photon constructs no
longer execute fabricated return values. Incomplete source examples/oracles
must be implemented before they compile.

The compiler suite covers indexed loops, condition branches, measurement/reset,
parameter/phase preservation, readout maps, small-circuit equivalence, directed
and disconnected topology, and exact native matrices. Provider contract tests
run offline; optional installed-SDK checks validate real schemas and bitcode.
Hardware smoke tests must be separately configured with credentials and budget.

The [acceptance checklist](implementation-status.md) maps the implementation to
the requested plan and separates external verification from code delivery.
