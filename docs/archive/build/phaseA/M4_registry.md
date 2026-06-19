# M4 — Registry (YAML schema + loader + 4-chip demo)

## 1. Goal & spec section

The per-chip data infrastructure that lets one compiler serve a
dozen-plus machines without a dozen-plus code paths. Implements
[Spinor Engineering Deep-Dive][deep-dive] Part 2 §2 and Part 3 §2.

The registry is what makes the Definition-of-Done item "supporting a
new chip is a YAML file, not a code change" real.

[deep-dive]: ../../spinor_engineering_deep_dive.docx

## 2. Inputs / outputs

- **Consumes:**
  - YAML files under `spinor/registry/chips/<id>.yaml` and
    `spinor/registry/topologies/<name>.yaml`.
  - Optional calibration JSON under
    `~/.cache/spinor/calibration/<id>.json` (a stub for now;
    refresh job lives in Phase D).
- **Produces:**
  - `spinor::registry::Registry` — a typed in-memory map of
    chip id → `ChipInfo`.
  - `verify::TargetInfo` snapshots derivable from `ChipInfo`
    (the M3 stub is replaced by the loader).
- **Invariants:** every `ChipInfo` has every required field;
  every native gate name is recognised by the dialect; every
  topology referenced exists and is parseable; field values
  are within documented ranges.

## 3. Deliverables

- `spinor/registry/include/spinor/registry/Registry.h` — public
  API.
- `spinor/registry/lib/Yaml.h` + `Yaml.cpp` — small YAML
  subset parser (mappings, sequences, scalars, comments).
- `spinor/registry/lib/Loader.cpp` — schema validation, typed
  loader, calibration shim.
- `spinor/registry/lib/Topologies.cpp` — topology
  generator helpers (`linear_n`, `all_to_all`, full edge-list
  topologies).
- `spinor/registry/CMakeLists.txt`
- `spinor/registry/topologies/{heavy_hex_156,linear_n,all_to_all}.yaml`
- `spinor/registry/chips/ibm_heron_r2.yaml`
- `spinor/registry/chips/ionq_forte.yaml`
- `spinor/registry/chips/quantinuum_helios.yaml`
- `spinor/registry/chips/ionq_aria_proto.yaml` (the 4th chip)
- `spinor/registry/schema/spinor-registry.schema.json` —
  human + machine-readable JSON schema (informational).
- `spinor/tests/m4/CMakeLists.txt`
- `spinor/tests/m4/yaml_test.cpp` — YAML subset parser unit tests.
- `spinor/tests/m4/loader_test.cpp` — load all 4 chips, reject
  malformed entries, derive `TargetInfo`.
- `spinor/tests/m4/no_code_change_test.cpp` — proof-by-doing:
  the 4th chip routes/decomposes through the same code paths
  as the first three (this asserts the structural property
  required by the Phase A definition of done; full
  routing/decomposition lands at M5/M6, but the loader half
  is M4).

## 4. Data structures

```c++
namespace spinor::registry {

struct DecomposeRecipe {
  enum class OneQubit { EulerZyz };
  enum class TwoQubit { Kak };
  OneQubit oneQubit = OneQubit::EulerZyz;
  TwoQubit twoQubit = TwoQubit::Kak;
  std::string oneQubitRotationGate;   // e.g. "rz"
  std::string oneQubitPi2Gate;        // e.g. "sx"; "" if none
  std::string twoQubitEntangler;      // e.g. "ecr"
  int twoQubitEntanglerCountMax = 3;  // KAK bound
};

struct CapabilityFlags {
  bool midCircuitMeasure = true;
  enum class Feedforward { None, Limited, Full };
  Feedforward feedforward = Feedforward::None;
  bool reset = true;
};

struct ChipInfo {
  std::string id;
  std::string provider;             // ibm | aws | azure | rigetti | ionq | quantinuum | local
  std::size_t qubits = 0;
  std::vector<std::string> nativeGates;

  // Coupling: either a named topology + size (linear/all-to-all),
  // or an explicit edge list. Resolved at load.
  bool allToAll = false;
  std::vector<std::pair<int, int>> coupling;  // sorted, low<high

  CapabilityFlags supports;
  DecomposeRecipe decompose;

  double pricePerShotUsd = 0.0;
  std::optional<double> pricePerMinuteUsd;

  std::string notes;
  std::filesystem::path calibrationStore;  // optional path
  std::string calibrationSource;           // ibm_runtime_api | braket_api | azure_api | fixture
  std::string calibrationRefresh;          // nightly | daily | never | fixture
};

class Registry {
 public:
  // Load all chips/ and topologies/ under a root path.
  static Registry load(const std::filesystem::path& root,
                       dialect::Diagnostics& diag);

  bool has(const std::string& id) const;
  const ChipInfo& get(const std::string& id) const;
  std::vector<std::string> ids() const;

  // Adapter to M3.
  verify::TargetInfo targetInfo(const std::string& id) const;
};

}  // namespace spinor::registry
```

## 5. Algorithms

### YAML subset parser

The registry only uses:

- block mappings (`key: value`)
- block sequences (`- item`)
- flow sequences (`[a, b, c]`)
- scalars (strings, numbers, booleans, null)
- comments (`# ...`)
- multi-line `|` strings (for `notes`)

We do NOT need anchors, aliases, tags, complex flow mappings,
or `<<` merge keys. A 200-line parser handles the full registry
schema. Errors carry line numbers.

### Topology resolution

- `linear_n` with `size: N`: edges {(0,1), (1,2), …, (N-2,N-1)}.
- `all_to_all` with `size: N`: edges {(i,j) : 0≤i<j<N}; we set
  `allToAll = true` and leave `coupling` empty for memory.
- explicit edge list (e.g. `heavy_hex_156`): YAML file with a
  `qubits: 156` and `edges: [[a,b], [c,d], ...]` block.

### Schema validation

After load, every chip is checked:

- `id` matches the file basename.
- `qubits > 0`, equals topology size where relevant.
- every `native_gates` entry is a known Spinor dialect mnemonic
  (cross-check against `dialect::opMnemonic`).
- `decompose.one_qubit.recipe == "euler_zyz"`,
  `decompose.two_qubit.recipe == "kak"`.
- `decompose.two_qubit.entangler_count_max == 3` (KAK bound).
- if `coupling_map.topology` is named, it resolves; if it's an
  edge list, edges fit within `qubits`.

Validation failures are appended to a `Diagnostics` and the
ChipInfo is dropped from the Registry.

### TargetInfo adapter

Given a `ChipInfo`, build a `verify::TargetInfo`:

- `nativeGates` copied as-is.
- `coupling`/`allToAll` projected directly.
- `qubitCount = chip.qubits`.
- `midCircuitMeasure = chip.supports.midCircuitMeasure`.

## 6. Test matrix

| ID    | Name                                | Type        | Inputs                                  | Expected output                                              | CI gate         |
| ----- | ----------------------------------- | ----------- | --------------------------------------- | ------------------------------------------------------------ | --------------- |
| M4-T01 | `M4_yaml.parse_scalars`            | unit        | `key: 42`, `key: 3.14`, `key: "abc"`    | scalar values typed correctly                                | `ctest -L M4`   |
| M4-T02 | `M4_yaml.parse_block_mapping`      | unit        | nested key/value                         | nested map structure                                         | `ctest -L M4`   |
| M4-T03 | `M4_yaml.parse_block_sequence`     | unit        | `-` lines                                | sequence elements parsed                                     | `ctest -L M4`   |
| M4-T04 | `M4_yaml.parse_flow_sequence`      | unit        | `[a, b, c]`                              | flow sequence parsed                                         | `ctest -L M4`   |
| M4-T05 | `M4_yaml.parse_comments_skipped`   | unit        | `key: 1 # ignore`                        | only `key=1`                                                 | `ctest -L M4`   |
| M4-T06 | `M4_yaml.parse_multiline_string`   | unit        | `notes: \|\n  abc\n  def`                | concatenated string                                          | `ctest -L M4`   |
| M4-T07 | `M4_loader.load_ibm_heron_r2`      | integration | `chips/ibm_heron_r2.yaml`                | qubits=156, ecr/rz/sx/x in nativeGates                       | `ctest -L M4`   |
| M4-T08 | `M4_loader.load_ionq_forte`        | integration | `chips/ionq_forte.yaml`                  | allToAll=true, MS in nativeGates, no mid-circuit support     | `ctest -L M4`   |
| M4-T09 | `M4_loader.load_quantinuum_helios` | integration | `chips/quantinuum_helios.yaml`           | feedforward=Full, allToAll                                   | `ctest -L M4`   |
| M4-T10 | `M4_loader.load_ionq_aria_proto`   | integration | `chips/ionq_aria_proto.yaml`             | the 4th chip loads                                           | `ctest -L M4`   |
| M4-T11 | `M4_loader.reject_unknown_native_gate` | regression | malformed chip with `native_gates: [unknown]` | error msg names "unknown gate"                          | `ctest -L M4`   |
| M4-T12 | `M4_loader.reject_unknown_topology` | regression | malformed chip referencing missing topology | error msg names "topology"                              | `ctest -L M4`   |
| M4-T13 | `M4_loader.reject_id_mismatch`     | regression | chip's `id` ≠ filename                   | error msg names "id"                                         | `ctest -L M4`   |
| M4-T14 | `M4_loader.reject_kak_count_wrong` | regression | `entangler_count_max: 4`                 | error msg names "KAK"                                        | `ctest -L M4`   |
| M4-T15 | `M4_target_info.derives_correctly` | integration | each loaded chip                         | `targetInfo` agrees with `ChipInfo`                          | `ctest -L M4`   |
| M4-T16 | `M4_no_code_change.fourth_chip_loads_same_path` | integration | each chip                | identical loader path; identical structural shape            | `ctest -L M4`   |

## 7. Cookbook example — adding a new chip

The proof of the design: adding a chip is a YAML file. Suppose
we want to add `aws_rigetti_aspen` with 32 qubits, native set
`{cz, rx, rz}`, octagonal coupling map.

Step 1: write `spinor/registry/chips/aws_rigetti_aspen.yaml`
(with `coupling_map.topology: aspen_octagon_32`).

Step 2: write `spinor/registry/topologies/aspen_octagon_32.yaml`
with the explicit edge list.

Step 3: …that's it. No C++ change. Run `spinorc` against the
new id and the engine routes/decomposes/emits. Everything else
flows from the registry.

## 8. Pitfalls

- **YAML indentation.** Our subset is whitespace-sensitive
  block-style. Mixed tabs/spaces are rejected; the parser
  reports the offending line.
- **Empty native_gates.** Loader rejects a chip with no native
  gates: there is nothing decomposition could emit.
- **Topology size mismatch.** If a chip says `qubits: 156` but
  the heavy_hex_156 topology is missing or has 154 nodes, the
  loader errors clearly.
- **Calibration store path.** Tilde expansion (`~`) is the
  loader's responsibility; do not let raw `~/...` strings
  reach the filesystem.
- **Naming convention.** Chip ids use lowercase + underscores.
  The loader normalises to lowercase to match `target ...`
  comparisons.

## 9. Definition of Done

- [ ] Spec landed.
- [ ] All M4-T## tests pass.
- [ ] Four chips load cleanly (`ibm_heron_r2`, `ionq_forte`,
      `quantinuum_helios`, `ionq_aria_proto`).
- [ ] Registry schema documented in
      [registry-schema.md](registry-schema.md).
- [ ] Test matrix updated.
- [ ] Glossary updated for any new terms (none expected).
- [ ] Progress journal appended.

## 10. Open questions

None.
