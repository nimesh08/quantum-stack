// common/cli/tests/flags_test.cpp

#include "qs/common/cli/Flags.h"
#include "qs/common/cli/Manifest.h"
#include "qs/common/cli/Providers.h"
#include "qs/common/cli/Submit.h"
#include "test_main.h"

#include <fstream>
#include <string>

using namespace qs::common::cli;

namespace {

Flags parse(std::initializer_list<const char*> argv) {
  std::vector<const char*> a(argv);
  return parseArgv(static_cast<int>(a.size()), a.data());
}

}  // namespace

TEST(M1_flags, parses_subcommand) {
  auto f = parse({"photonc", "compile", "bell.pho", "--target", "ibm_heron_r2"});
  EXPECT_TRUE(f.subcommand == Subcommand::Compile);
  EXPECT_TRUE(f.target.has_value() && *f.target == "ibm_heron_r2");
  EXPECT_EQ(static_cast<int>(f.positionals.size()), 1);
  EXPECT_TRUE(f.positionals[0] == "bell.pho");
  EXPECT_TRUE(f.errors.empty());
}

TEST(M1_flags, parses_short_target_flag) {
  auto f = parse({"photonc", "run", "bell.pho", "-t", "ibm_brisbane"});
  EXPECT_TRUE(f.target.has_value() && *f.target == "ibm_brisbane");
}

TEST(M1_flags, parses_eq_form) {
  auto f = parse({"photonc", "compile", "bell.pho",
                  "--target=ibm_heron_r2", "--shots=2048", "--mode=cassette"});
  EXPECT_TRUE(*f.target == "ibm_heron_r2");
  EXPECT_TRUE(f.shots.has_value() && *f.shots == 2048);
  EXPECT_TRUE(f.mode == Mode::Cassette);
}

TEST(M1_flags, parses_emit_format) {
  auto f = parse({"photonc", "compile", "bell.pho",
                  "--target", "ibm_heron_r2", "--emit", "phonon"});
  EXPECT_TRUE(f.emit == EmitFormat::Phonon);
}

TEST(M1_flags, rejects_unknown_emit) {
  auto f = parse({"photonc", "compile", "bell.pho",
                  "--target", "ibm_heron_r2", "--emit", "wat"});
  EXPECT_FALSE(f.errors.empty());
}

TEST(M1_flags, rejects_secret_literal) {
  auto f = parse({"photonc", "run", "bell.pho",
                  "--target", "ibm_heron_r2",
                  "--api-key=AAAA-SECRET"});
  EXPECT_FALSE(f.errors.empty());
  bool found = false;
  for (auto& e : f.errors) if (e.find("secret") != std::string::npos) found = true;
  EXPECT_TRUE(found);
}

TEST(M1_flags, accepts_api_key_file) {
  auto f = parse({"photonc", "run", "bell.pho",
                  "--target", "ibm_heron_r2",
                  "--api-key-file", "/tmp/tok"});
  EXPECT_TRUE(f.errors.empty());
  EXPECT_TRUE(f.api_key_file.has_value() && *f.api_key_file == "/tmp/tok");
}

TEST(M1_flags, accepts_api_key_stdin) {
  auto f = parse({"photonc", "run", "bell.pho",
                  "--target", "ibm_heron_r2", "--api-key-stdin"});
  EXPECT_TRUE(f.errors.empty());
  EXPECT_TRUE(f.api_key_stdin);
}

TEST(M1_flags, parses_extra_repeatable) {
  auto f = parse({"photonc", "run", "bell.pho",
                  "--target", "ibm_heron_r2",
                  "--extra", "queue=urgent",
                  "--extra", "tier=premium"});
  EXPECT_EQ(static_cast<int>(f.extras.size()), 2);
  EXPECT_TRUE(f.extras["queue"] == "urgent");
  EXPECT_TRUE(f.extras["tier"]  == "premium");
}

TEST(M1_flags, validate_compile_needs_target) {
  auto f = parse({"photonc", "compile", "bell.pho"});
  auto v = validate(f);
  EXPECT_FALSE(v.empty());
}

TEST(M1_flags, validate_run_with_target_ok) {
  auto f = parse({"photonc", "run", "bell.pho",
                  "--target", "ibm_heron_r2"});
  auto v = validate(f);
  EXPECT_TRUE(v.empty());
}

TEST(M1_flags, help_renders_with_program_name) {
  std::string s = renderHelp("photonc", "the Photon language driver");
  EXPECT_CONTAINS(s, "photonc");
  EXPECT_CONTAINS(s, "the Photon language driver");
  EXPECT_CONTAINS(s, "--target");
  EXPECT_CONTAINS(s, "--api-key-file");
  EXPECT_CONTAINS(s, "Rule 5");
}

TEST(M1_providers, all_known_providers_present) {
  auto v = allProviders();
  bool ibm = false, aws = false, azure = false, qci = false;
  for (auto& p : v) {
    if (p.id == "ibm")      ibm = true;
    if (p.id == "aws")      aws = true;
    if (p.id == "azure")    azure = true;
    if (p.id == "qci")      qci = true;
  }
  EXPECT_TRUE(ibm);
  EXPECT_TRUE(aws);
  EXPECT_TRUE(azure);
  EXPECT_TRUE(qci);
}

TEST(M1_providers, find_returns_known_id) {
  auto* p = findProvider("ibm");
  EXPECT_TRUE(p != nullptr);
  EXPECT_TRUE(p->env_var == "IBM_QUANTUM_TOKEN");
}

TEST(M1_providers, find_returns_null_for_unknown) {
  auto* p = findProvider("definitely_not_real");
  EXPECT_TRUE(p == nullptr);
}

#ifdef SPINOR_REGISTRY_ROOT
TEST(M1_providers, lookup_chip_provider_from_yaml) {
  auto p = providerForChip(SPINOR_REGISTRY_ROOT, "ibm_heron_r2");
  EXPECT_TRUE(p.has_value() && *p == "ibm");
  auto q = providerForChip(SPINOR_REGISTRY_ROOT, "ionq_forte");
  EXPECT_TRUE(q.has_value() && *q == "aws");
  auto r = providerForChip(SPINOR_REGISTRY_ROOT, "qci_aqumen");
  EXPECT_TRUE(r.has_value() && *r == "qci");
  auto x = providerForChip(SPINOR_REGISTRY_ROOT, "definitely_not_real");
  EXPECT_FALSE(x.has_value());
}
#endif

TEST(M1_submit, build_argv_basic) {
  SubmitRequest r;
  r.qasm_path = "out.qasm3";
  r.chip = "ibm_heron_r2";
  r.provider = "ibm";
  r.shots = 1000;
  r.mode = Mode::Cassette;
  r.program_name = "bell";
  auto a = buildPythonArgv(r);
  // first non-python args
  bool saw_module = false;
  bool saw_submit = false;
  bool saw_chip = false;
  bool saw_provider = false;
  bool saw_mode = false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] == "-m" && i + 1 < a.size() && a[i+1] == "spinor_submit")
      saw_module = true;
    if (a[i] == "submit") saw_submit = true;
    if (a[i] == "--chip" && i + 1 < a.size() && a[i+1] == "ibm_heron_r2")
      saw_chip = true;
    if (a[i] == "--provider" && i + 1 < a.size() && a[i+1] == "ibm")
      saw_provider = true;
    if (a[i] == "--mode" && i + 1 < a.size() && a[i+1] == "cassette")
      saw_mode = true;
  }
  EXPECT_TRUE(saw_module);
  EXPECT_TRUE(saw_submit);
  EXPECT_TRUE(saw_chip);
  EXPECT_TRUE(saw_provider);
  EXPECT_TRUE(saw_mode);
}

TEST(M1_submit, build_argv_with_api_key_file_and_extras) {
  SubmitRequest r;
  r.qasm_path = "out.qasm3";
  r.chip = "ibm_heron_r2";
  r.provider = "ibm";
  r.shots = 100;
  r.mode = Mode::Live;
  r.api_key_file = "/tmp/token";
  r.extras["queue"] = "urgent";
  auto a = buildPythonArgv(r);
  bool saw_keyfile = false;
  bool saw_extra = false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] == "--api-key-file" && i + 1 < a.size() && a[i+1] == "/tmp/token")
      saw_keyfile = true;
    if (a[i] == "--extra" && i + 1 < a.size() && a[i+1] == "queue=urgent")
      saw_extra = true;
  }
  EXPECT_TRUE(saw_keyfile);
  EXPECT_TRUE(saw_extra);
}

TEST(M1_manifest, sha256_known_vector) {
  // SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
  std::string tmp = "/tmp/qs_manifest_test_abc.txt";
  { std::ofstream f(tmp); f << "abc"; }
  auto h = sha256OfFile(tmp);
  EXPECT_TRUE(h ==
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(M1_manifest, sha256_empty_file) {
  std::string tmp = "/tmp/qs_manifest_test_empty.txt";
  { std::ofstream f(tmp); }
  auto h = sha256OfFile(tmp);
  // SHA-256 of empty input.
  EXPECT_TRUE(h ==
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(M1_manifest, json_round_trip_keys) {
  Manifest m;
  m.source_lang = "photon";
  m.source_path = "bell.pho";
  m.source_sha256 = "deadbeef";
  m.target = "ibm_heron_r2";
  m.provider = "ibm";
  m.verbatim = true;
  m.estimate.num_qubits = 2;
  m.estimate.two_qubit_count = 1;
  m.estimate.depth = 4;
  m.shots = 1024;
  m.cost_usd_estimate = 0.34;
  m.produced_by_binary = "photonc";
  m.produced_by_version = "0.1.0";
  m.produced_by_git_sha = "deadbeef";
  m.produced_at_utc = "2026-06-17T08:00:00Z";
  auto s = toJson(m);
  EXPECT_CONTAINS(s, "\"schema\":            \"heisenberg.manifest/v1\"");
  EXPECT_CONTAINS(s, "\"source_lang\":       \"photon\"");
  EXPECT_CONTAINS(s, "\"target\":            \"ibm_heron_r2\"");
  EXPECT_CONTAINS(s, "\"verbatim\":          true");
  EXPECT_CONTAINS(s, "\"num_qubits\": 2");
  EXPECT_CONTAINS(s, "\"two_qubit_count\": 1");
  EXPECT_CONTAINS(s, "\"depth\": 4");
}

TEST(M1_manifest, iso8601_timestamp_well_formed) {
  std::string ts = nowUtcIso8601();
  EXPECT_EQ(static_cast<int>(ts.size()), 20);
  EXPECT_TRUE(ts[4] == '-');
  EXPECT_TRUE(ts[7] == '-');
  EXPECT_TRUE(ts[10] == 'T');
  EXPECT_TRUE(ts[13] == ':');
  EXPECT_TRUE(ts[16] == ':');
  EXPECT_TRUE(ts[19] == 'Z');
}

SPINOR_TEST_MAIN()
