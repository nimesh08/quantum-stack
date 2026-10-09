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
    "  spinorc emit     -t <chip-id> -f <qasm3|qir|quil|json> [-O 0|1|2|3] "
                       "[--verbatim] <FILE.spn>\n"
    "  spinorc check    -t <chip-id> [-O 0|1|2|3] <FILE.spn>\n"
    "  spinorc run      -t <chip-id> [-O 0|1|2|3] [--shots N] "
                       "[--token T] [--instance CRN] <FILE.spn>\n"
    "  spinorc submit   -t <chip-id> [--provider P] [--mode m] [--shots N] "
                       "<FILE.qasm3>\n"
    "  spinorc simulate -t <chip> [--compiled] [--shots N] [--seed N] <FILE>\n"
    "  spinorc registry list\n"
    "\n"
    "optimization levels (compile/emit/check/run):\n"
    "  -O 0   no optimization (raw post-decomposition IR)\n"
    "  -O 1   exact local simplification\n"
    "  -O 2   commutation, block optimization and topology routing (default)\n"
    "  -O 3   bounded layout alternatives and native resynthesis\n"
    "\n"
    "Native synthesis, routing, and optimization are performed by Spinor.\n";
}

// Parse -O <level> from CLI. Returns the default level if not
// present. Invalid requests fail rather than silently changing the level.
spinor::passes::OptimizationLevel parseOLevel(int argc, char** argv) {
  auto v = argValue(argc, argv, "-O");
  if (!v) return spinor::passes::kDefaultOptimizationLevel;
  auto level = spinor::passes::parseOptimizationLevel(v->c_str());
  // The internal convenience parser defaults invalid strings; the public CLI
  // must reject them so a successful compile reflects the requested level.
  const std::string& s = *v;
  bool ok = (s == "0" || s == "1" || s == "2" || s == "3" ||
             s == "O0" || s == "O1" || s == "O2" || s == "O3");
  if (!ok) {
    throw std::runtime_error("optimization level must be 0, 1, 2 or 3");
  }
  return level;
}

}  // namespace

int main(int argc, char** argv) try {
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
    if (!target) target = argValue(argc, argv, "--target");
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
    if (!target) target = argValue(argc, argv, "--target");
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
    if (hasFlag(argc, argv, "--help")) {
      std::cout << "usage: spinorc emit -t <chip> -f <qasm3|qir|quil|json>"
                   " [-O 0|1|2|3] [--verbatim] <FILE.spn>\n";
      return 0;
    }
    auto target = argValue(argc, argv, "-t");
    if (!target) target = argValue(argc, argv, "--target");
    auto format = argValue(argc, argv, "-f");
    if (!format) format = argValue(argc, argv, "--format");
    if (!format) format = argValue(argc, argv, "--emit");
    bool verbatim = hasFlag(argc, argv, "--verbatim");
    if (!target || !format || argc < 6) {
      std::cerr << "usage: spinorc emit -t <chip> -f <qasm3|qir|quil|json>"
                << " [-O 0|1|2|3] [--verbatim] <FILE.spn>\n";
      return 2;
    }
    auto level = parseOLevel(argc, argv);
    std::string file = argv[argc - 1];
    auto source=slurp(file);
    spinor::parser::ParseResult r;
    if(hasFlag(argc,argv,"--compiled"))r.module=spinor::dialect::parse(source,r.diag);
    else r=spinor::parser::parse(source,file);
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
    auto cleaned = hasFlag(argc,argv,"--compiled") ? *r.module : pm.compile(*r.module, chip, level, pmDiag);
    if (hasFlag(argc,argv,"--compiled"))spinor::passes::validateCompiled(cleaned,chip,pmDiag);
    if (pmDiag.hasErrors()) { dumpDiagnostics(pmDiag); return 1; }

    if (*format == "qasm3") {
      spinor::emit::EmitOptions opts;
      opts.braketVerbatim = verbatim;
      std::cout << spinor::emit::emitQasm3(cleaned, &chip, opts);
    } else if (*format == "qir") {
      std::cout << spinor::emit::emitQir(cleaned, &chip);
    } else if (*format == "json") {
      std::cout << spinor::emit::emitPhysicalJson(cleaned);
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
    if (!target) target = argValue(argc, argv, "--target");
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
    spinor::sim::EquivResult eq;
    bool checked=false;std::string uncheckedReason;
    try {
      eq = spinor::sim::equivalent(*r.module, decomposed);
      checked=true;
    }catch(const std::exception& e){uncheckedReason=e.what();}
    auto est = spinor::sim::estimate(decomposed, &chip);
    if (checked) {
      std::cout << "exhaustive unitary equivalence: " << (eq.equivalent ? "ok" : "MISMATCH")
                << " (diff=" << eq.maxAbsDiff << ")\n";
    } else {
      std::cout << "equivalence: not checked (" << uncheckedReason << ")\n";
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
    return !checked ? 2 : (eq.equivalent ? 0 : 1);
  }

  if (cmd == "simulate") {
    auto target=argValue(argc,argv,"-t");
    if(!target)target=argValue(argc,argv,"--target");
    if(!target||argc<5)throw std::runtime_error("usage: spinorc simulate -t CHIP [--compiled] [--shots N] [--seed N] FILE");
    auto parseUnsigned=[&](const std::string& flag,std::uint64_t fallback){
      auto value=argValue(argc,argv,flag);if(!value)return fallback;
      if(value->empty()||value->front()=='-')throw std::runtime_error(flag+" must be a nonnegative integer");
      std::size_t end=0;auto n=std::stoull(*value,&end);
      if(end!=value->size())throw std::runtime_error("invalid "+flag);
      return static_cast<std::uint64_t>(n);
    };
    auto shots=parseUnsigned("--shots",1024), seed=parseUnsigned("--seed",42);
    std::string file=argv[argc-1];auto source=slurp(file);
    spinor::dialect::Diagnostics d;
    auto reg=spinor::registry::Registry::load(defaultRegistryRoot(),d);
    if(!reg.has(*target))throw std::runtime_error("unknown chip id: "+*target);
    std::optional<spinor::dialect::Module> m;
    if(hasFlag(argc,argv,"--compiled"))m=spinor::dialect::parse(source,d);
    else {
      auto r=spinor::parser::parse(source,file);
      if(!r.module){dumpDiagnostics(r.diag);return 1;}
      spinor::passes::PassManager pm;
      m=pm.compile(*r.module,reg.get(*target),parseOLevel(argc,argv),d);
    }
    if(m&&hasFlag(argc,argv,"--compiled"))spinor::passes::validateCompiled(*m,reg.get(*target),d);
    if(!m||d.hasErrors()){dumpDiagnostics(d);return 1;}
    spinor::submit::LocalProvider provider(seed);
    auto job=provider.submit(*m,reg.get(*target),shots);auto result=provider.results(job.id);
    std::cout << "{\"provider\":\"local\",\"status\":\"completed\",\"shots\":" << shots << ",\"counts\":{";
    bool first=true;for(const auto& [bits,count]:result.counts){
      if(!first)std::cout << ',';first=false;
      std::cout << '\"' << bits << "\":" << count;
    }
    std::cout << "}}\n";
    return 0;
  }

  if (cmd == "run" || cmd == "submit") {
    std::vector<std::string> args(argv+1,argv+argc);
    auto result=qs::common::cli::runQstack(args);
    std::cout << result.stdout_text;
    std::cerr << result.stderr_text;
    return result.exit_code;
  }

  std::cerr << "unknown subcommand: " << cmd << "\n";
  return 2;
} catch (const std::exception& e) {
  std::cerr << "error: " << e.what() << "\n";
  return 1;
}
