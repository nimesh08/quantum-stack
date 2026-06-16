// spinor/tests/m4/loader_test.cpp
//
// M4 loader and TargetInfo derivation tests.

#include "spinor/registry/Registry.h"
#include "spinor/dialect/Spinor.h"
#include "test_main.h"

#include <filesystem>
#include <fstream>

using namespace spinor::registry;
using spinor::dialect::Diagnostics;

#ifndef SPINOR_REGISTRY_ROOT
#define SPINOR_REGISTRY_ROOT "."
#endif

#ifndef SPINOR_M4_FIXTURE_ROOT
#define SPINOR_M4_FIXTURE_ROOT "."
#endif

static const std::filesystem::path kRoot = SPINOR_REGISTRY_ROOT;
static const std::filesystem::path kFixtures = SPINOR_M4_FIXTURE_ROOT;

TEST(M4_loader, load_ibm_heron_r2) {
  Diagnostics d;
  Registry r = Registry::load(kRoot, d);
  EXPECT_FALSE(d.hasErrors());
  EXPECT_TRUE(r.has("ibm_heron_r2"));
  const ChipInfo& c = r.get("ibm_heron_r2");
  EXPECT_EQ(static_cast<size_t>(c.qubits), static_cast<size_t>(156));
  EXPECT_FALSE(c.allToAll);
  EXPECT_EQ(c.nativeGates.size(), static_cast<size_t>(4));
  EXPECT_EQ(c.decompose.twoQubitEntangler, std::string("ecr"));
  EXPECT_EQ(c.decompose.oneQubitPi2Gate, std::string("sx"));
}

TEST(M4_loader, load_ionq_forte) {
  Diagnostics d;
  Registry r = Registry::load(kRoot, d);
  EXPECT_FALSE(d.hasErrors());
  EXPECT_TRUE(r.has("ionq_forte"));
  const ChipInfo& c = r.get("ionq_forte");
  EXPECT_TRUE(c.allToAll);
  EXPECT_EQ(static_cast<size_t>(c.qubits), static_cast<size_t>(36));
  EXPECT_FALSE(c.supports.midCircuitMeasure);
}

TEST(M4_loader, load_quantinuum_helios) {
  Diagnostics d;
  Registry r = Registry::load(kRoot, d);
  EXPECT_FALSE(d.hasErrors());
  EXPECT_TRUE(r.has("quantinuum_helios"));
  const ChipInfo& c = r.get("quantinuum_helios");
  EXPECT_TRUE(c.supports.midCircuitMeasure);
  EXPECT_TRUE(c.supports.feedforward == CapabilityFlags::Feedforward::Full);
}

TEST(M4_loader, load_ionq_aria_proto) {
  Diagnostics d;
  Registry r = Registry::load(kRoot, d);
  EXPECT_FALSE(d.hasErrors());
  EXPECT_TRUE(r.has("ionq_aria_proto"));
  // The 4th chip — proves no code change is needed.
  const ChipInfo& c = r.get("ionq_aria_proto");
  EXPECT_EQ(static_cast<size_t>(c.qubits), static_cast<size_t>(25));
  EXPECT_TRUE(c.allToAll);
}

TEST(M4_loader, target_info_derives_correctly) {
  Diagnostics d;
  Registry r = Registry::load(kRoot, d);
  for (const auto& id : r.ids()) {
    auto t = r.targetInfo(id);
    const ChipInfo& c = r.get(id);
    EXPECT_EQ(t.id, c.id);
    EXPECT_EQ(static_cast<size_t>(t.qubitCount),
              static_cast<size_t>(c.qubits));
    EXPECT_EQ(t.allToAll, c.allToAll);
    EXPECT_EQ(t.midCircuitMeasure, c.supports.midCircuitMeasure);
  }
}

TEST(M4_loader, four_chips_load_same_path) {
  // Proves the YAML-only-new-chip Definition of Done: all four
  // chips are produced by the same loader path; no chip needed
  // a code change.
  Diagnostics d;
  Registry r = Registry::load(kRoot, d);
  EXPECT_EQ(static_cast<size_t>(r.size()), static_cast<size_t>(4));
  std::vector<std::string> ids = r.ids();
  EXPECT_TRUE(std::find(ids.begin(), ids.end(),
                        std::string("ibm_heron_r2")) != ids.end());
  EXPECT_TRUE(std::find(ids.begin(), ids.end(),
                        std::string("ionq_forte")) != ids.end());
  EXPECT_TRUE(std::find(ids.begin(), ids.end(),
                        std::string("quantinuum_helios")) != ids.end());
  EXPECT_TRUE(std::find(ids.begin(), ids.end(),
                        std::string("ionq_aria_proto")) != ids.end());
}

// ---- malformed-fixture rejection ----

namespace {

void writeFixture(const std::filesystem::path& dir,
                  const std::string& filename,
                  const std::string& body) {
  std::filesystem::create_directories(dir / "chips");
  std::filesystem::create_directories(dir / "topologies");
  std::ofstream f(dir / "chips" / filename);
  f << body;
}

}  // namespace

TEST(M4_loader, reject_unknown_native_gate) {
  std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "spinor_m4_bad1";
  std::filesystem::remove_all(tmp);
  writeFixture(tmp, "fake_chip.yaml", R"(
id: fake_chip
provider: local
qubits: 2
native_gates: [not_a_real_gate]
coupling_map: { topology: linear_n, size: 2 }
decomposition:
  one_qubit:
    recipe: euler_zyz
    rotation_gate: rz
  two_qubit:
    recipe: kak
    entangler: ecr
    entangler_count_max: 3
)");
  Diagnostics d;
  Registry r = Registry::load(tmp, d);
  EXPECT_TRUE(d.hasErrors());
  EXPECT_FALSE(r.has("fake_chip"));
  std::string joined;
  for (const auto& di : d.items()) joined += di.message + "\n";
  EXPECT_CONTAINS(joined, "unknown gate");
}

TEST(M4_loader, reject_id_mismatch) {
  std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "spinor_m4_bad2";
  std::filesystem::remove_all(tmp);
  writeFixture(tmp, "fake_chip.yaml", R"(
id: not_fake_chip
provider: local
qubits: 2
native_gates: [h]
coupling_map: { topology: linear_n, size: 2 }
decomposition:
  one_qubit:
    recipe: euler_zyz
    rotation_gate: rz
  two_qubit:
    recipe: kak
    entangler: ecr
    entangler_count_max: 3
)");
  Diagnostics d;
  Registry r = Registry::load(tmp, d);
  EXPECT_TRUE(d.hasErrors());
  std::string joined;
  for (const auto& di : d.items()) joined += di.message + "\n";
  EXPECT_CONTAINS(joined, "id");
}

TEST(M4_loader, reject_kak_count_wrong) {
  std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "spinor_m4_bad3";
  std::filesystem::remove_all(tmp);
  writeFixture(tmp, "fake_chip.yaml", R"(
id: fake_chip
provider: local
qubits: 2
native_gates: [h, cx]
coupling_map: { topology: linear_n, size: 2 }
decomposition:
  one_qubit:
    recipe: euler_zyz
    rotation_gate: rz
  two_qubit:
    recipe: kak
    entangler: cx
    entangler_count_max: 5
)");
  Diagnostics d;
  Registry r = Registry::load(tmp, d);
  EXPECT_TRUE(d.hasErrors());
  std::string joined;
  for (const auto& di : d.items()) joined += di.message + "\n";
  EXPECT_CONTAINS(joined, "KAK");
}

SPINOR_TEST_MAIN()
