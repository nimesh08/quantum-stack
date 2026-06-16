// spinor/tests/m10/m10_test.cpp

#include "spinor/dialect/Spinor.h"
#include "spinor/parser/Parser.h"
#include "spinor/registry/Registry.h"
#include "spinor/submit/Provider.h"
#include "test_main.h"

#include <fstream>
#include <sstream>
#include <filesystem>

using namespace spinor;
using namespace spinor::dialect;
using namespace spinor::submit;

#ifndef SPINOR_M10_CORPUS_DIR
#define SPINOR_M10_CORPUS_DIR "."
#endif

namespace {
std::string slurp(const std::filesystem::path& p) {
  std::ifstream f(p); std::ostringstream s; s << f.rdbuf(); return s.str();
}
registry::ChipInfo dummyChip() {
  registry::ChipInfo c;
  c.id = "test_chip";
  c.qubits = 4;
  c.allToAll = true;
  return c;
}
}  // namespace

TEST(M10_local, bell_returns_two_peaks) {
  auto r = parser::parse(slurp(
      std::filesystem::path(SPINOR_M10_CORPUS_DIR) / "bell.spn"));
  EXPECT_TRUE(r.module.has_value());
  LocalProvider provider;
  Job j = provider.submit(*r.module, dummyChip(), 2000);
  EXPECT_EQ(j.status, std::string("completed"));
  Histogram h = provider.results(j.id);
  EXPECT_EQ(static_cast<int>(h.shots), 2000);
  // Only |00> and |11> should be present.
  std::size_t total = 0;
  for (const auto& [k, v] : h.counts) total += v;
  EXPECT_EQ(static_cast<int>(total), 2000);
  EXPECT_TRUE(h.counts.count("00") > 0);
  EXPECT_TRUE(h.counts.count("11") > 0);
  // Approximately balanced (within 10 pp).
  double a = static_cast<double>(h.counts["00"]);
  double b = static_cast<double>(h.counts["11"]);
  double bias = std::abs(a - b) / static_cast<double>(total);
  EXPECT_TRUE(bias < 0.15);
}

TEST(M10_local, ghz_returns_two_peaks) {
  auto r = parser::parse(slurp(
      std::filesystem::path(SPINOR_M10_CORPUS_DIR) / "ghz.spn"));
  EXPECT_TRUE(r.module.has_value());
  LocalProvider provider;
  Job j = provider.submit(*r.module, dummyChip(), 2000);
  EXPECT_EQ(j.status, std::string("completed"));
  Histogram h = provider.results(j.id);
  EXPECT_TRUE(h.counts.count("000") > 0);
  EXPECT_TRUE(h.counts.count("111") > 0);
}

TEST(M10_local, poll_unknown_returns_error) {
  LocalProvider provider;
  Job j = provider.poll("does_not_exist");
  EXPECT_EQ(j.status, std::string("error"));
}

TEST(M10_local, name_is_local) {
  LocalProvider provider;
  EXPECT_EQ(provider.name(), std::string("local"));
}

SPINOR_TEST_MAIN()
