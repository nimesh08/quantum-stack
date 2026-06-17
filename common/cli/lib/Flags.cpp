// common/cli/lib/Flags.cpp

#include "qs/common/cli/Flags.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace qs::common::cli {

std::string toString(Subcommand s) {
  switch (s) {
    case Subcommand::Compile:   return "compile";
    case Subcommand::Estimate:  return "estimate";
    case Subcommand::Submit:    return "submit";
    case Subcommand::Run:       return "run";
    case Subcommand::Targets:   return "targets";
    case Subcommand::Providers: return "providers";
    case Subcommand::Version:   return "version";
    case Subcommand::Help:      return "help";
    case Subcommand::Unknown:   return "<unknown>";
  }
  return "<unknown>";
}

Subcommand parseSubcommand(std::string_view word) {
  if (word == "compile")   return Subcommand::Compile;
  if (word == "estimate")  return Subcommand::Estimate;
  if (word == "submit")    return Subcommand::Submit;
  if (word == "run")       return Subcommand::Run;
  if (word == "targets")   return Subcommand::Targets;
  if (word == "providers") return Subcommand::Providers;
  if (word == "version")   return Subcommand::Version;
  if (word == "help" || word == "--help" || word == "-h")
    return Subcommand::Help;
  return Subcommand::Unknown;
}

std::string toString(Mode m) {
  switch (m) {
    case Mode::Cassette: return "cassette";
    case Mode::Live:     return "live";
    case Mode::Local:    return "local";
  }
  return "cassette";
}

std::optional<Mode> parseMode(std::string_view w) {
  if (w == "cassette") return Mode::Cassette;
  if (w == "live")     return Mode::Live;
  if (w == "local")    return Mode::Local;
  return std::nullopt;
}

std::string toString(EmitFormat f) {
  switch (f) {
    case EmitFormat::Qasm3:  return "qasm3";
    case EmitFormat::Qir:    return "qir";
    case EmitFormat::Quil:   return "quil";
    case EmitFormat::Phonon: return "phonon";
    case EmitFormat::Spinor: return "spinor";
  }
  return "qasm3";
}

std::optional<EmitFormat> parseEmitFormat(std::string_view w) {
  if (w == "qasm3")  return EmitFormat::Qasm3;
  if (w == "qir")    return EmitFormat::Qir;
  if (w == "quil")   return EmitFormat::Quil;
  if (w == "phonon") return EmitFormat::Phonon;
  if (w == "spinor") return EmitFormat::Spinor;
  return std::nullopt;
}

namespace {

// Helper: pull --flag and --flag=value forms uniformly.
struct ArgIter {
  int argc;
  const char* const* argv;
  int pos = 1;

  bool hasNext() const { return pos < argc; }
  std::string_view current() const { return argv[pos]; }
  void advance() { ++pos; }
};

// `--api-key=<literal>` is intentionally rejected per the
// open-cli-collective standard (process listing / shell history leak).
// Detect this and return a precise diagnostic.
bool isSecretLiteral(std::string_view a) {
  static constexpr const char* kBanned[] = {
    "--api-key", "--token", "--bearer", "--password", "--secret",
  };
  for (const char* b : kBanned) {
    auto bl = std::string_view(b);
    if (a.size() > bl.size() + 1 && a.substr(0, bl.size()) == bl &&
        a[bl.size()] == '=') {
      return true;
    }
  }
  return false;
}

// Pull the value for a flag like `--target X` or `--target=X`. After
// the call, `it.pos` points just past the consumed argument(s).
std::optional<std::string> takeValue(ArgIter& it, std::string_view flag,
                                     std::vector<std::string>& errors) {
  std::string_view a = it.current();
  if (a == flag) {
    it.advance();
    if (!it.hasNext()) {
      errors.emplace_back(std::string(flag) + " requires a value");
      return std::nullopt;
    }
    std::string v(it.current());
    it.advance();
    return v;
  }
  // --flag=value
  std::string fEq = std::string(flag) + "=";
  if (a.size() > fEq.size() && a.substr(0, fEq.size()) == fEq) {
    std::string v(a.substr(fEq.size()));
    it.advance();
    return v;
  }
  return std::nullopt;
}

bool takeBool(ArgIter& it, std::string_view flag) {
  if (it.current() == flag) {
    it.advance();
    return true;
  }
  return false;
}

}  // namespace

Flags parseArgv(int argc, const char* const* argv) {
  Flags f;
  if (argc < 2) {
    f.subcommand = Subcommand::Help;
    return f;
  }
  // The first non-flag word is the subcommand.
  ArgIter it{argc, argv, 1};
  // Allow leading global flags like `-v`, `--help`.
  while (it.hasNext()) {
    std::string_view a = it.current();
    if (a == "-h" || a == "--help") {
      f.subcommand = Subcommand::Help;
      return f;
    }
    if (a == "-v" || a == "--verbose") { f.verbose = true; it.advance(); continue; }
    if (a == "-q" || a == "--quiet")   { f.quiet   = true; it.advance(); continue; }
    if (!a.empty() && a[0] != '-') break;
    // Unknown global flag - keep processing as if subcommand-local.
    break;
  }
  if (!it.hasNext()) {
    f.subcommand = Subcommand::Help;
    return f;
  }
  f.subcommand = parseSubcommand(it.current());
  if (f.subcommand == Subcommand::Unknown) {
    f.errors.emplace_back("unknown subcommand: " + std::string(it.current()));
    return f;
  }
  if (f.subcommand == Subcommand::Help ||
      f.subcommand == Subcommand::Version) {
    return f;
  }
  it.advance();

  // Subcommand-local options.
  while (it.hasNext()) {
    std::string_view a = it.current();

    if (isSecretLiteral(a)) {
      f.errors.emplace_back(
          "secret-bearing flag must use --api-key-file <path> "
          "or --api-key-stdin (refusing literal: " +
          std::string(a.substr(0, a.find('='))) + "=...)");
      it.advance();
      continue;
    }

    auto v = takeValue(it, "--target", f.errors);
    if (v) { f.target = std::move(*v); continue; }
    v = takeValue(it, "-t", f.errors);
    if (v) { f.target = std::move(*v); continue; }
    v = takeValue(it, "--provider", f.errors);
    if (v) { f.provider = std::move(*v); continue; }
    v = takeValue(it, "--url", f.errors);
    if (v) { f.url = std::move(*v); continue; }
    v = takeValue(it, "--region", f.errors);
    if (v) { f.region = std::move(*v); continue; }
    v = takeValue(it, "--instance", f.errors);
    if (v) { f.instance = std::move(*v); continue; }
    v = takeValue(it, "--api-key-file", f.errors);
    if (v) { f.api_key_file = std::move(*v); continue; }
    v = takeValue(it, "--config", f.errors);
    if (v) { f.config_file = std::move(*v); continue; }
    v = takeValue(it, "--shots", f.errors);
    if (v) {
      try { f.shots = std::stoi(*v); }
      catch (...) { f.errors.emplace_back("--shots requires an integer"); }
      continue;
    }
    v = takeValue(it, "--cost-cap-usd", f.errors);
    if (v) {
      try { f.cost_cap_usd = std::stod(*v); }
      catch (...) { f.errors.emplace_back("--cost-cap-usd requires a number"); }
      continue;
    }
    v = takeValue(it, "--mode", f.errors);
    if (v) {
      auto m = parseMode(*v);
      if (m) f.mode = *m;
      else f.errors.emplace_back(
          "--mode must be one of: cassette, live, local (got: " + *v + ")");
      continue;
    }
    v = takeValue(it, "--emit", f.errors);
    if (v) {
      auto e = parseEmitFormat(*v);
      if (e) f.emit = *e;
      else f.errors.emplace_back(
          "--emit must be one of: qasm3, qir, quil, phonon, spinor "
          "(got: " + *v + ")");
      continue;
    }
    v = takeValue(it, "-o", f.errors);
    if (v) { f.output_path = std::move(*v); continue; }
    v = takeValue(it, "--out", f.errors);
    if (v) { f.output_path = std::move(*v); continue; }
    v = takeValue(it, "--extra", f.errors);
    if (v) {
      auto eq = v->find('=');
      if (eq == std::string::npos) {
        f.errors.emplace_back("--extra requires k=v form");
      } else {
        f.extras.emplace(v->substr(0, eq), v->substr(eq + 1));
      }
      continue;
    }

    if (takeBool(it, "--api-key-stdin")) { f.api_key_stdin = true; continue; }
    if (takeBool(it, "--manifest"))      { f.write_manifest = true; continue; }
    if (takeBool(it, "--verbatim"))      { f.verbatim = true; continue; }
    if (takeBool(it, "--no-verbatim"))   { f.verbatim = false; continue; }
    if (takeBool(it, "-v") || takeBool(it, "--verbose")) {
      f.verbose = true; continue;
    }
    if (takeBool(it, "-q") || takeBool(it, "--quiet")) {
      f.quiet = true; continue;
    }
    if (takeBool(it, "-h") || takeBool(it, "--help")) {
      f.subcommand = Subcommand::Help; return f;
    }

    if (!a.empty() && a[0] == '-') {
      f.errors.emplace_back("unknown flag: " + std::string(a));
      it.advance();
      continue;
    }
    // Positional argument.
    f.positionals.emplace_back(a);
    it.advance();
  }
  return f;
}

std::string renderHelp(std::string_view programName,
                       std::string_view shortDesc) {
  std::ostringstream os;
  os << programName << " - " << shortDesc << "\n\n"
     << "Usage:\n"
     << "  " << programName << " <subcommand> [options] <input>\n\n"
     << "Subcommands:\n"
     << "  compile    Lower a source file to a portable artifact (qasm3/qir/quil/phonon/spinor)\n"
     << "  estimate   Print the resource estimate for a source file (qubits, two-qubit count, depth, dollar cost)\n"
     << "  submit     Submit an already-compiled QASM3 file to a provider\n"
     << "  run        Compile + submit in one step\n"
     << "  targets    List registry chips\n"
     << "  providers  List supported providers and their flag conventions\n"
     << "  version    Print binary version and git SHA\n"
     << "  help       Print this help\n\n"
     << "Options (universal):\n"
     << "  -t, --target <chip>      Chip id (auto-derives --provider from registry)\n"
     << "      --shots <N>          Shot count (default 1024)\n"
     << "      --mode <m>           Submit mode: cassette (default), live, local\n"
     << "      --provider <p>       Provider id (overrides auto-derived)\n"
     << "      --api-key-file <p>   Path to a one-line file containing the secret\n"
     << "      --api-key-stdin      Read the secret from stdin once\n"
     << "      --url <u>            Provider REST endpoint override\n"
     << "      --region <r>         AWS / Azure region\n"
     << "      --instance <i>       IBM hub-group-project / Azure workspace\n"
     << "      --extra k=v          Provider-specific escape hatch (repeatable)\n"
     << "      --emit <fmt>         Output format: qasm3 (default), qir, quil, phonon, spinor\n"
     << "  -o, --out <path>         Write artifact to <path> (default: stdout)\n"
     << "      --manifest           Also write <path>.manifest.json\n"
     << "      --verbatim           Pass through to provider (default ON; Rule 5)\n"
     << "      --no-verbatim        Disable verbatim (NOT recommended)\n"
     << "      --cost-cap-usd <X>   Refuse to submit above this dollar cost\n"
     << "      --config <path>      Extra config file (default: ~/.config/heisenberg/config.toml)\n"
     << "  -v, --verbose            Print every stage's command line (gcc-style)\n"
     << "  -q, --quiet              Suppress info logs\n"
     << "  -h, --help               This help\n\n"
     << "Secret handling:\n"
     << "  Secret-bearing flags are NEVER accepted as bare CLI args (process\n"
     << "  listing + shell history leak). Use --api-key-file <path> or\n"
     << "  --api-key-stdin or set the provider-specific env var (e.g.\n"
     << "  IBM_QUANTUM_TOKEN). See `" << programName << " providers` for the\n"
     << "  full list per provider.\n";
  return os.str();
}

std::vector<std::string> validate(const Flags& f) {
  std::vector<std::string> errs;
  switch (f.subcommand) {
    case Subcommand::Compile:
    case Subcommand::Estimate:
    case Subcommand::Run:
      if (!f.target) errs.emplace_back(toString(f.subcommand) + ": --target <chip> is required");
      if (f.positionals.empty())
        errs.emplace_back(toString(f.subcommand) + ": missing input file");
      break;
    case Subcommand::Submit:
      if (f.positionals.empty())
        errs.emplace_back("submit: missing QASM3 file");
      if (!f.target) errs.emplace_back("submit: --target <chip> is required");
      break;
    case Subcommand::Targets:
    case Subcommand::Providers:
    case Subcommand::Version:
    case Subcommand::Help:
    case Subcommand::Unknown:
      break;
  }
  return errs;
}

}  // namespace qs::common::cli
