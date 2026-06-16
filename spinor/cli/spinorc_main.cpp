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
#include "spinor/passes/Placement.h"
#include "spinor/passes/Routing.h"
#include "spinor/registry/Registry.h"
#include "spinor/sim/Simulator.h"
#include "spinor/submit/Provider.h"
#include "spinor/verify/Verifier.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: spinorc <parse|verify|compile|registry> [args]\n";
    return 2;
  }
  std::string cmd = argv[1];

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
    auto target = argValue(argc, argv, "-t");
    if (!target || argc < 5) {
      std::cerr << "usage: spinorc compile -t <chip-id> <FILE.spn>\n";
      return 2;
    }
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
    spinor::passes::CouplingGraph g(chip.qubits, chip.coupling, chip.allToAll);
    spinor::passes::Placement pl;
    auto layout = pl.run(*r.module, g);
    spinor::passes::Routing routing;
    auto routed = routing.run(*r.module, chip, g, layout);
    spinor::passes::Decomposition dec;
    spinor::dialect::Diagnostics decDiag;
    auto decomposed = dec.run(routed.module, chip, decDiag);
    spinor::passes::Cleanup cl;
    auto cleaned = cl.run(decomposed);
    if (decDiag.hasErrors()) {
      dumpDiagnostics(decDiag);
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
    // spinorc emit -t <chip> -f <qasm3|qir|quil> [--verbatim] FILE.spn
    auto target = argValue(argc, argv, "-t");
    auto format = argValue(argc, argv, "-f");
    bool verbatim = false;
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--verbatim") verbatim = true;
    }
    if (!target || !format || argc < 6) {
      std::cerr << "usage: spinorc emit -t <chip> -f <qasm3|qir|quil>"
                << " [--verbatim] <FILE.spn>\n";
      return 2;
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
    spinor::passes::CouplingGraph g(chip.qubits, chip.coupling, chip.allToAll);
    spinor::passes::Placement pl;
    auto layout = pl.run(*r.module, g);
    spinor::passes::Routing routing;
    auto routed = routing.run(*r.module, chip, g, layout);
    spinor::passes::Decomposition dec;
    spinor::dialect::Diagnostics decDiag;
    auto decomposed = dec.run(routed.module, chip, decDiag);
    spinor::passes::Cleanup cl;
    auto cleaned = cl.run(decomposed);
    if (decDiag.hasErrors()) { dumpDiagnostics(decDiag); return 1; }

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
    // spinorc check -t <chip> FILE.spn
    auto target = argValue(argc, argv, "-t");
    if (!target || argc < 5) {
      std::cerr << "usage: spinorc check -t <chip> <FILE.spn>\n";
      return 2;
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
    spinor::passes::CouplingGraph g(chip.qubits, chip.coupling, chip.allToAll);
    spinor::passes::Placement pl;
    auto layout = pl.run(*r.module, g);
    spinor::passes::Routing routing;
    auto routed = routing.run(*r.module, chip, g, layout);
    spinor::passes::Decomposition dec;
    spinor::dialect::Diagnostics decDiag;
    auto decomposed = dec.run(routed.module, chip, decDiag);
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

  std::cerr << "unknown subcommand: " << cmd << "\n";
  return 2;
}
