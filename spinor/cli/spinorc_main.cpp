// spinor/cli/spinorc_main.cpp
//
// `spinorc` — Phase A driver. Subcommands:
//   parse      <file>            : parse and print the IR
//   verify     -t <id> <file>    : parse + W1-W6 verifier
//   compile    -t <id> <file>    : parse + place + route + decompose + cleanup
//   registry   list              : list known chip ids
//
// Wraps M1 (dialect), M3 (verifier), M4 (registry), M5
// (placement + routing), M6 (decomposition + cleanup), M7
// (production parser).

#include "spinor/dialect/Spinor.h"
#include "spinor/emit/Emitters.h"
#include "spinor/parser/Parser.h"
#include "spinor/passes/CouplingGraph.h"
#include "spinor/passes/Decomposition.h"
#include "spinor/passes/OptimizationLevel.h"
#include "spinor/passes/PassManager.h"
#include "spinor/passes/Placement.h"
#include "spinor/passes/Routing.h"
#include "spinor/registry/Registry.h"
#include "spinor/sim/Simulator.h"
#include "spinor/submit/Provider.h"
#include "spinor/verify/Verifier.h"

#include "qs/common/cli/Providers.h"
#include "qs/common/cli/Submit.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string slurp(const std::filesystem::path& p) {
  std::ifstream f(p);
  if (!f) {
    std::cerr << "cannot read " << p.string() << "\n";
    std::exit(1);
  }
  std::ostringstream s; s << f.rdbuf(); return s.str();
}

void dumpDiagnostics(const spinor::dialect::Diagnostics& d) {
  for (const auto& di : d.items()) {
    const char* sev = (di.severity == spinor::dialect::DiagSeverity::Error)
                          ? "error"
                          : "warning";
    std::cerr << sev;
    if (!di.loc.file.empty() || di.loc.line > 0) {
      std::cerr << " " << di.loc.file << ":" << di.loc.line << ":"
                << di.loc.column;
    }
    std::cerr << ": " << di.message << "\n";
  }
}

std::filesystem::path defaultRegistryRoot() {
  // Prefer SPINOR_REGISTRY_ROOT env var if set; otherwise look up
  // the source-tree default (only useful in CI).
  if (const char* p = std::getenv("SPINOR_REGISTRY_ROOT")) {
    return p;
  }
  return "spinor/registry";
}

std::optional<std::string> argValue(int argc, char** argv,
                                    const std::string& flag) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == flag) return argv[i + 1];
  }
  return std::nullopt;
}

bool hasFlag(int argc, char** argv, const std::string& flag) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == flag) return true;
  }
  return false;
}

void printHelp() {
  std::cout <<
    "spinorc — Spinor compiler driver\n"
    "\n"
    "usage:\n"
    "  spinorc parse    <FILE.spn>\n"
    "  spinorc verify   -t <chip-id> <FILE.spn>\n"
    "  spinorc compile  -t <chip-id> [-O 0|1|2|3] <FILE.spn>\n"
    "  spinorc emit     -t <chip-id> -f <qasm3|qir|quil> [-O 0|1|2|3] "
                       "[--verbatim] <FILE.spn>\n"
    "  spinorc check    -t <chip-id> [-O 0|1|2|3] <FILE.spn>\n"
    "  spinorc run      -t <chip-id> [-O 0|1|2|3] [--shots N] "
                       "[--token T] [--instance CRN] <FILE.spn>\n"
    "  spinorc submit   -t <chip-id> [--provider P] [--mode m] [--shots N] "
                       "<FILE.qasm3>\n"
    "  spinorc registry list\n"
    "\n"
    "optimization levels (compile/emit/check/run):\n"
    "  -O 0   no optimization (raw post-decomposition IR)\n"
    "  -O 1   light: peephole cleanup (default)\n"
    "  -O 2   medium: + commutative cancellation\n"
    "  -O 3   heavy: + 2Q block KAK resynthesis (Cartan)\n"
    "\n"
    "auto-routing:\n"
    "  When the target chip's provider is \"ibm\", `spinorc emit`\n"
    "  emits standard Qiskit Python (the same code a normal Qiskit\n"
    "  user would write) regardless of -f, and `spinorc run` spawns\n"
    "  python3 to execute it (transpile + SamplerV2 submission).\n";
}

// Parse -O <level> from CLI. Returns the default level if not
// present. Emits a warning to stderr on invalid values.
spinor::passes::OptimizationLevel parseOLevel(int argc, char** argv) {
  auto v = argValue(argc, argv, "-O");
  if (!v) return spinor::passes::kDefaultOptimizationLevel;
  auto level = spinor::passes::parseOptimizationLevel(v->c_str());
  // Validate: parser returns the default for invalid input; warn
  // if the user passed a non-canonical value.
  const std::string& s = *v;
  bool ok = (s == "0" || s == "1" || s == "2" || s == "3" ||
             s == "O0" || s == "O1" || s == "O2" || s == "O3");
  if (!ok) {
    std::cerr << "warning: -O '" << s
              << "' is not 0/1/2/3; defaulting to -O1\n";
  }
  return level;
}

// Pick the python3 binary to use for `spinorc run`. Order of
// preference:
//   1. $SPINOR_QISKIT_PYTHON (explicit override)
//   2. <build-dir>/.qiskit-venv/bin/python3 (rebuild-compiler.sh
//      installs Qiskit here)
//   3. python3 from $PATH
std::string resolvePython() {
  if (const char* p = std::getenv("SPINOR_QISKIT_PYTHON")) {
    if (*p) return p;
  }
  // Look up the build directory from $SPINOR_REGISTRY_ROOT (which
  // points at <repo>/spinor/registry → its parent is <repo>/spinor,
  // grandparent is <repo>, build is <repo>/build).
  if (const char* p = std::getenv("SPINOR_REGISTRY_ROOT")) {
    std::filesystem::path root(p);
    std::filesystem::path candidate =
        root.parent_path().parent_path() / "build" / ".qiskit-venv" / "bin" / "python3";
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
      return candidate.string();
    }
  }
  return "python3";
}

// Try to load IBM credentials from a JSON file. Accepts the same
// shape as `qs::common::cli::SubmitRequest` writes: a JSON object
// with "token" and "instance" keys (other keys ignored).
struct IbmCreds {
  std::string token;
  std::string instance;
  bool valid() const { return !token.empty() && !instance.empty(); }
};

IbmCreds readCredsFile(const std::string& path) {
  IbmCreds c;
  std::ifstream f(path);
  if (!f) return c;
  std::ostringstream s; s << f.rdbuf();
  std::string blob = s.str();
  // Tiny ad-hoc JSON value extractor: find "key" : "value".
  auto pull = [&](const std::string& key) -> std::string {
    std::string needle = "\"" + key + "\"";
    auto pos = blob.find(needle);
    if (pos == std::string::npos) return {};
    auto colon = blob.find(':', pos + needle.size());
    if (colon == std::string::npos) return {};
    auto q1 = blob.find('"', colon + 1);
    if (q1 == std::string::npos) return {};
    auto q2 = blob.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return blob.substr(q1 + 1, q2 - q1 - 1);
  };
  c.token = pull("token");
  c.instance = pull("instance");
  return c;
}

// Spawn `python <script>` with `extra_env` augmenting the current
// environment. Capture stdout into `stdout_out`. Returns python's
// exit code.
int runPython(const std::string& python_bin,
              const std::string& script_path,
              const std::vector<std::pair<std::string, std::string>>& extra_env,
              std::string& stdout_out) {
  // Build the command. We use popen() with a shell so quoting is
  // straightforward; injection isn't a concern because both
  // python_bin and script_path are paths we constructed.
  std::ostringstream cmd;
  for (const auto& [k, v] : extra_env) {
    // Single-quote the value, escaping inner single-quotes by
    // closing the quote, inserting \', and reopening.
    cmd << k << "='";
    for (char c : v) {
      if (c == '\'') cmd << "'\\''";
      else cmd << c;
    }
    cmd << "' ";
  }
  cmd << "'" << python_bin << "' '" << script_path << "' 2>/dev/null";
  FILE* p = popen(cmd.str().c_str(), "r");
  if (!p) return -1;
  std::array<char, 4096> buf;
  size_t n = 0;
  while ((n = std::fread(buf.data(), 1, buf.size(), p)) > 0) {
    stdout_out.append(buf.data(), n);
  }
  int rc = pclose(p);
  // WEXITSTATUS-equivalent
  if (rc == -1) return -1;
  return (rc >> 8) & 0xff;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: spinorc <parse|verify|compile|emit|check|run|submit|registry> [args]\n";
    std::cerr << "       spinorc --help    for full usage\n";
    return 2;
  }
  std::string cmd = argv[1];

  if (cmd == "--help" || cmd == "-h" || cmd == "help") {
    printHelp();
    return 0;
  }

  if (cmd == "parse") {
    if (argc < 3) {
      std::cerr << "usage: spinorc parse <FILE.spn>\n";
      return 2;
    }
    auto r = spinor::parser::parse(slurp(argv[2]), argv[2]);
    if (!r.module) {
      dumpDiagnostics(r.diag);
      return 1;
    }
    std::cout << print(*r.module);
    return 0;
  }

  if (cmd == "verify") {
    auto target = argValue(argc, argv, "-t");
    if (!target || argc < 5) {
      std::cerr << "usage: spinorc verify -t <chip-id> <FILE.spn>\n";
      return 2;
    }
    std::string file = argv[argc - 1];
    auto r = spinor::parser::parse(slurp(file), file);
    if (!r.module) {
      dumpDiagnostics(r.diag);
      return 1;
    }
    spinor::verify::TargetInfo t;
    if (*target == "generic") {
      t = spinor::verify::generic_target();
    } else {
      spinor::dialect::Diagnostics d;
      auto reg = spinor::registry::Registry::load(defaultRegistryRoot(), d);
      if (!reg.has(*target)) {
        std::cerr << "unknown chip id: " << *target << "\n";
        dumpDiagnostics(d);
        return 1;
      }
      t = reg.targetInfo(*target);
    }
    spinor::dialect::Diagnostics vd;
    bool ok = spinor::verify::verify(*r.module, t, vd);
    dumpDiagnostics(vd);
    return ok ? 0 : 1;
  }

  if (cmd == "compile") {
    if (hasFlag(argc, argv, "--help")) {
      std::cout << "usage: spinorc compile -t <chip-id> [-O 0|1|2|3] <FILE.spn>\n";
      return 0;
    }
    auto target = argValue(argc, argv, "-t");
    if (!target || argc < 5) {
      std::cerr << "usage: spinorc compile -t <chip-id> [-O 0|1|2|3] <FILE.spn>\n";
      return 2;
    }
    auto level = parseOLevel(argc, argv);
    std::string file = argv[argc - 1];
    auto r = spinor::parser::parse(slurp(file), file);
    if (!r.module) {
      dumpDiagnostics(r.diag);
      return 1;
    }
    spinor::dialect::Diagnostics d;
    auto reg = spinor::registry::Registry::load(defaultRegistryRoot(), d);
    if (!reg.has(*target)) {
      std::cerr << "unknown chip id: " << *target << "\n";
      dumpDiagnostics(d);
      return 1;
    }
    const auto& chip = reg.get(*target);
    spinor::dialect::Diagnostics pmDiag;
    spinor::passes::PassManager pm;
    auto cleaned = pm.compile(*r.module, chip, level, pmDiag);
    if (pmDiag.hasErrors()) {
      dumpDiagnostics(pmDiag);
      return 1;
    }
    std::cout << print(cleaned);
    return 0;
  }

  if (cmd == "registry" && argc >= 3 &&
      std::string(argv[2]) == "list") {
    spinor::dialect::Diagnostics d;
    auto reg = spinor::registry::Registry::load(defaultRegistryRoot(), d);
    for (const auto& id : reg.ids()) std::cout << id << "\n";
    if (d.hasErrors()) dumpDiagnostics(d);
    return 0;
  }

  if (cmd == "emit") {
    // spinorc emit -t <chip> -f <qasm3|qir|quil> [-O 0|1|2|3]
    //              [--verbatim] FILE.spn
    //
    // Auto-routing: when chip.provider == "ibm", we IGNORE the
    // -f flag and emit standard Qiskit Python (the same code a
    // normal Qiskit user would write). The user / IDE then either
    // saves the Python to a file or pipes it into `spinorc run`,
    // which spawns python3 to execute the transpile + submission.
    // This buys us Qiskit-quality optimization on Heron without
    // re-implementing the full Qiskit transpiler in C++.
    if (hasFlag(argc, argv, "--help")) {
      std::cout << "usage: spinorc emit -t <chip> -f <qasm3|qir|quil>"
                   " [-O 0|1|2|3] [--verbatim] <FILE.spn>\n";
      return 0;
    }
    auto target = argValue(argc, argv, "-t");
    auto format = argValue(argc, argv, "-f");
    bool verbatim = hasFlag(argc, argv, "--verbatim");
    if (!target || !format || argc < 6) {
      std::cerr << "usage: spinorc emit -t <chip> -f <qasm3|qir|quil>"
                << " [-O 0|1|2|3] [--verbatim] <FILE.spn>\n";
      return 2;
    }
    auto level = parseOLevel(argc, argv);
    std::string file = argv[argc - 1];
    auto r = spinor::parser::parse(slurp(file), file);
    if (!r.module) { dumpDiagnostics(r.diag); return 1; }
    spinor::dialect::Diagnostics d;
    auto reg = spinor::registry::Registry::load(defaultRegistryRoot(), d);
    if (!reg.has(*target)) {
      std::cerr << "unknown chip id: " << *target << "\n";
      return 1;
    }
    const auto& chip = reg.get(*target);

    // ---- IBM auto-routing: emit Qiskit Python and stop ----
    if (chip.provider == "ibm") {
      // Use the universal-gate IR straight from parse. Skip
      // place/route/decompose/cleanup — Qiskit's transpile() will
      // do all that, better than our scaffolded passes do today.
      int lvl = static_cast<int>(level);
      std::cout << spinor::emit::emitQiskitPython(*r.module, chip, lvl);
      return 0;
    }

    // ---- Native path (non-IBM vendors) ----
    spinor::dialect::Diagnostics pmDiag;
    spinor::passes::PassManager pm;
    auto cleaned = pm.compile(*r.module, chip, level, pmDiag);
    if (pmDiag.hasErrors()) { dumpDiagnostics(pmDiag); return 1; }

    if (*format == "qasm3") {
      spinor::emit::EmitOptions opts;
      opts.braketVerbatim = verbatim;
      std::cout << spinor::emit::emitQasm3(cleaned, &chip, opts);
    } else if (*format == "qir") {
      std::cout << spinor::emit::emitQir(cleaned, &chip);
    } else if (*format == "quil") {
      std::cout << spinor::emit::emitQuil(cleaned);
    } else {
      std::cerr << "unknown format: " << *format << "\n";
      return 2;
    }
    return 0;
  }

  if (cmd == "check") {
    // spinorc check -t <chip> [-O 0|1|2|3] FILE.spn
    if (hasFlag(argc, argv, "--help")) {
      std::cout << "usage: spinorc check -t <chip> [-O 0|1|2|3] <FILE.spn>\n";
      return 0;
    }
    auto target = argValue(argc, argv, "-t");
    if (!target || argc < 5) {
      std::cerr << "usage: spinorc check -t <chip> [-O 0|1|2|3] <FILE.spn>\n";
      return 2;
    }
    auto level = parseOLevel(argc, argv);
    std::string file = argv[argc - 1];
    auto r = spinor::parser::parse(slurp(file), file);
    if (!r.module) { dumpDiagnostics(r.diag); return 1; }
    spinor::dialect::Diagnostics d;
    auto reg = spinor::registry::Registry::load(defaultRegistryRoot(), d);
    if (!reg.has(*target)) {
      std::cerr << "unknown chip id: " << *target << "\n";
      return 1;
    }
    const auto& chip = reg.get(*target);
    spinor::dialect::Diagnostics pmDiag;
    spinor::passes::PassManager pm;
    auto decomposed = pm.compile(*r.module, chip, level, pmDiag);
    if (pmDiag.hasErrors()) { dumpDiagnostics(pmDiag); return 1; }
    bool canSim = (chip.qubits <= 12);
    spinor::sim::EquivResult eq;
    if (canSim) {
      eq = spinor::sim::equivalent(*r.module, decomposed);
    }
    auto est = spinor::sim::estimate(decomposed, &chip);
    if (canSim) {
      std::cout << "equivalence: " << (eq.equivalent ? "ok" : "MISMATCH")
                << " (diff=" << eq.maxAbsDiff << ")\n";
    } else {
      std::cout << "equivalence: skipped (chip has " << chip.qubits
                << " qubits; statevector capped at 12)\n";
    }
    std::cout << "gates total:        " << est.totalGates << "\n";
    std::cout << "gates two-qubit:    " << est.twoQubitGates << "\n";
    std::cout << "depth:              " << est.depth << "\n";
    std::cout << "qubits:             " << est.qubits << "\n";
    std::cout << "measurements:       " << est.measurements << "\n";
    if (est.totalErrorEstimate)
      std::cout << "error (est.):       " << *est.totalErrorEstimate << "\n";
    if (est.shotCostUsd)
      std::cout << "cost @1k shots ($): " << *est.shotCostUsd << "\n";
    return (canSim && !eq.equivalent) ? 1 : 0;
  }

  if (cmd == "run") {
    // spinorc run -t <chip> [-O 0|1|2|3] [--shots N]
    //             [--api-key-file PATH] [--token T] [--instance CRN]
    //             <FILE.spn>
    //
    // Compiles the Spinor source to Qiskit Python (only valid for
    // IBM targets right now), then spawns python3 to execute it.
    // Python prints a single-line JSON to stdout containing
    // {"histogram", "job_id", "depth", "gates", ...}; we echo that
    // verbatim and exit with python's exit code.
    if (hasFlag(argc, argv, "--help")) {
      std::cout <<
        "usage: spinorc run -t <chip> [-O 0|1|2|3] [--shots N]\n"
        "                  [--api-key-file PATH] [--token T] [--instance CRN]\n"
        "                  <FILE.spn>\n";
      return 0;
    }
    auto target = argValue(argc, argv, "-t");
    if (!target || argc < 5) {
      std::cerr << "usage: spinorc run -t <chip> [-O ...] [--shots N] "
                   "[--api-key-file PATH] [--token T] [--instance CRN] "
                   "<FILE.spn>\n";
      return 2;
    }
    auto level = parseOLevel(argc, argv);
    int shots = 1024;
    if (auto s = argValue(argc, argv, "--shots")) {
      try { shots = std::stoi(*s); } catch (...) {}
    }
    std::string file = argv[argc - 1];

    auto r = spinor::parser::parse(slurp(file), file);
    if (!r.module) { dumpDiagnostics(r.diag); return 1; }
    spinor::dialect::Diagnostics d;
    auto reg = spinor::registry::Registry::load(defaultRegistryRoot(), d);
    if (!reg.has(*target)) {
      std::cerr << "unknown chip id: " << *target << "\n";
      return 1;
    }
    const auto& chip = reg.get(*target);
    if (chip.provider != "ibm") {
      std::cerr << "spinorc run currently supports only IBM targets "
                   "(chip.provider == \"ibm\"); got '" << chip.provider
                << "' for chip " << chip.id
                << ". Use `spinorc emit` / `spinorc submit` for other vendors.\n";
      return 2;
    }

    // 1. Emit the Qiskit Python program.
    int lvl = static_cast<int>(level);
    std::string py = spinor::emit::emitQiskitPython(*r.module, chip, lvl);

    // 2. Write to a temp file so python3 can execute it.
    namespace fs = std::filesystem;
    fs::path tmpdir = fs::temp_directory_path() /
                      ("spinorc-run-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmpdir, ec);
    fs::path script = tmpdir / "program.py";
    {
      std::ofstream f(script);
      f << py;
    }

    // 3. Resolve credentials.
    std::string token, instance;
    if (auto t = argValue(argc, argv, "--token"))    token = *t;
    if (auto i = argValue(argc, argv, "--instance")) instance = *i;
    if (token.empty() || instance.empty()) {
      if (auto kf = argValue(argc, argv, "--api-key-file")) {
        IbmCreds c = readCredsFile(*kf);
        if (token.empty())    token    = c.token;
        if (instance.empty()) instance = c.instance;
      }
    }
    if (token.empty()) {
      if (const char* e = std::getenv("QISKIT_IBM_TOKEN"))    token = e;
    }
    if (instance.empty()) {
      if (const char* e = std::getenv("QISKIT_IBM_INSTANCE")) instance = e;
    }
    if (token.empty() || instance.empty()) {
      std::cerr <<
        "spinorc run: missing IBM credentials. Pass --token + --instance,\n"
        "or set QISKIT_IBM_TOKEN + QISKIT_IBM_INSTANCE env vars,\n"
        "or use --api-key-file <json>.\n";
      return 2;
    }

    // 4. Run python.
    std::string pybin = resolvePython();
    std::vector<std::pair<std::string, std::string>> env_extra = {
      {"QISKIT_IBM_TOKEN", token},
      {"QISKIT_IBM_INSTANCE", instance},
      {"SPINOR_SHOTS", std::to_string(shots)},
    };
    std::string out;
    int rc = runPython(pybin, script.string(), env_extra, out);
    std::cout << out;
    // Best-effort temp cleanup; don't fail the call on cleanup error.
    fs::remove_all(tmpdir, ec);
    return rc;
  }

  if (cmd == "submit") {
    // spinorc submit -t <chip> [--provider P] [--mode m] [--shots N]
    //                [--api-key-file path] FILE.qasm3
    auto target = argValue(argc, argv, "-t");
    if (!target) target = argValue(argc, argv, "--target");
    if (!target || argc < 5) {
      std::cerr << "usage: spinorc submit -t <chip> [--provider P] "
                   "[--mode cassette|live|local] [--shots N] "
                   "[--api-key-file PATH] <FILE.qasm3>\n";
      return 2;
    }
    std::string file = argv[argc - 1];
    qs::common::cli::SubmitRequest r;
    r.qasm_path = file;
    r.chip      = *target;
    auto prov = argValue(argc, argv, "--provider");
    if (prov) {
      r.provider = *prov;
    } else {
      // Auto-derive from the chip YAML.
      std::string root = defaultRegistryRoot().string();
      auto p = qs::common::cli::providerForChip(root, r.chip);
      r.provider = p.value_or("ibm");
    }
    if (auto s = argValue(argc, argv, "--shots")) {
      try { r.shots = std::stoi(*s); } catch (...) {}
    }
    if (auto m = argValue(argc, argv, "--mode")) {
      auto pm = qs::common::cli::parseMode(*m);
      if (pm) r.mode = *pm;
    }
    if (auto kf = argValue(argc, argv, "--api-key-file")) {
      r.api_key_file = *kf;
    }
    if (auto u = argValue(argc, argv, "--url"))      r.url      = *u;
    if (auto reg = argValue(argc, argv, "--region")) r.region   = *reg;
    if (auto inst = argValue(argc, argv, "--instance"))
      r.instance = *inst;
    if (auto pn = argValue(argc, argv, "--program-name")) {
      r.program_name = *pn;
    } else {
      std::filesystem::path p(file);
      r.program_name = p.stem().string();
      if (r.program_name.empty()) r.program_name = "bell";
    }
    auto result = qs::common::cli::runPython(r);
    std::cout << result.stdout_text;
    if (!result.ok) {
      std::cerr << result.stderr_text;
      return result.exit_code == 0 ? 1 : result.exit_code;
    }
    return 0;
  }

  std::cerr << "unknown subcommand: " << cmd << "\n";
  return 2;
}
