// phonon/cli/phononc_main.cpp
//
// `phononc` - middle-of-stack driver for the Phonon IR.
//
// Pipeline:
//   .phonon   -> (passthrough)        -> spinorc compile/emit/check
//   .spinor   -> (passthrough)        -> spinorc compile/emit/check
//
// The .pho path is intentionally NOT supported here - that's photonc's
// job. phononc is the right tool for users who write Phonon IR by hand
// or who feed it from a different frontend.
//
// Submission goes through `python -m spinor_submit` via the shared
// common::cli::runPython helper.

#include "qs/common/cli/Flags.h"
#include "phonon/parser/Parser.h"
#include "phonon/lower/Lowering.h"
#include "phonon/types/LinearTypeChecker.h"
#include "qs/common/cli/Manifest.h"
#include "qs/common/cli/Providers.h"
#include "qs/common/cli/Submit.h"

#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <process.h>
#  define _qs_pid() static_cast<long>(::_getpid())
#else
#  include <unistd.h>
#  define _qs_pid() static_cast<long>(::getpid())
#endif

#ifndef PHONONC_VERSION
#  define PHONONC_VERSION "0.1.0"
#endif

namespace fs = std::filesystem;
using namespace qs::common::cli;

namespace {

constexpr const char* kProgName = "phononc";
constexpr const char* kShortDesc =
    "Phonon IR driver (middle of the Heisenberg stack)";

std::string slurp(const fs::path& p) {
  std::ifstream f(p);
  if (!f) {
    std::cerr << "phononc: cannot read " << p.string() << "\n";
    std::exit(1);
  }
  std::ostringstream s; s << f.rdbuf();
  return s.str();
}

void writeFile(const fs::path& p, std::string_view text) {
  std::ofstream f(p);
  f << text;
}

fs::path defaultRegistryRoot() {
  if (const char* p = std::getenv("SPINOR_REGISTRY_ROOT")) return p;
  return "spinor/registry";
}

std::string findSpinorc() {
  if (const char* p = std::getenv("QSTACK_SPINORC")) {
    if (fs::exists(p)) return p;
  }
  const char* path = std::getenv("PATH");
  if (path) {
    std::string s = path; std::string token; std::stringstream ss(s);
    while (std::getline(ss, token,
#ifdef _WIN32
        ';'
#else
        ':'
#endif
        )) {
      fs::path cand = fs::path(token) /
#ifdef _WIN32
          "spinorc.exe";
#else
          "spinorc";
#endif
      if (fs::exists(cand)) return cand.string();
    }
  }
  fs::path here = "build/spinor/cli/spinorc";
  if (fs::exists(here)) return fs::absolute(here).string();
  return "spinorc";
}

struct ProcResult { int rc = 0; std::string out; };
ProcResult runProc(const std::vector<std::string>& argv) {
  auto result = runProcess(argv);
  if (!result.stderr_text.empty()) std::cerr << result.stderr_text;
  ProcResult out;
  out.rc = result.exit_code;
  out.out = std::move(result.stdout_text);
  return out;
}

int cmdTargets(const Flags&) {
  auto sc = findSpinorc();

  auto r = runProc({sc, "registry", "list"});
  std::cout << r.out;
  return r.rc;
}

int cmdProviders(const Flags&) {
  std::cout << "Provider id     Env var                       Description\n";
  std::cout << "--------------  ----------------------------  -----------\n";
  for (const auto& p : allProviders()) {
    char line[512];
    std::snprintf(line, sizeof(line), "%-15s %-29s %s\n",
                  p.id.c_str(), p.env_var.c_str(), p.description.c_str());
    std::cout << line;
  }
  std::cout << "\nSecret-bearing flags must use --api-key-file <path>\n";
  std::cout << "or --api-key-stdin or env (no --api-key=<literal>).\n";
  return 0;
}

int cmdVersion(const Flags&) {
  std::cout << kProgName << " " << PHONONC_VERSION << "\n";
  std::cout << "engine: spinorc subprocess\n";
  std::cout << "lowering subprocess: " << findSpinorc() << "\n";
  return 0;
}

std::string prepareSpinorFile(const std::string& input, const std::string& target) {
  auto parsed = phonon::parser::parse(slurp(input), input);
  if (!parsed.module) {
    for (const auto& d : parsed.diag.items()) std::cerr << d.message << "\n";
    std::exit(1);
  }
  parsed.module->targetAttr = target;
  phonon::dialect::Diagnostics diag;
  phonon::types::Options options;
  options.midCircuitMeasure = true;
  if (!phonon::types::typecheck(*parsed.module, options, diag)) {
    for (const auto& d : diag.items()) std::cerr << d.message << "\n";
    std::exit(1);
  }
  auto lowered = phonon::lower::lower(*parsed.module);
  if (!lowered.module) {
    for (const auto& d : lowered.diag.items()) std::cerr << d.message << "\n";
    std::exit(1);
  }
  auto tmp = fs::temp_directory_path() /
      (std::string("phononc_") + std::to_string(_qs_pid()) + ".spn");
  writeFile(tmp, phonon::lower::emitSpinorSource(*lowered.module));
  return tmp.string();
}

int cmdCompile(const Flags& f) {
  if (!f.target) { std::cerr << "compile: --target required\n"; return 2; }
  if (f.positionals.empty()) {
    std::cerr << "compile: missing input file\n"; return 2;
  }
  std::string in = prepareSpinorFile(f.positionals[0], *f.target);
  auto sc = findSpinorc();

  std::string outPath = f.output_path.value_or("");
  if (f.emit == EmitFormat::Phonon) {
    // Pass-through.
    std::string body = slurp(in);
    if (outPath.empty()) std::cout << body;
    else writeFile(outPath, body);
  } else if (f.emit == EmitFormat::Spinor) {
    std::vector<std::string> command{sc,"compile","-t",*f.target,"-O",std::to_string(f.optimization_level)};
    if(f.numerical_report)command.insert(command.end(),{"--numerical-report",*f.numerical_report});
    if(f.logical_ir_output)command.insert(command.end(),{"--logical-ir-output",*f.logical_ir_output});
    command.push_back(in);
    auto r = runProc(command);
    if (r.rc != 0) { std::cerr << r.out; return r.rc; }
    if (outPath.empty()) std::cout << r.out;
    else writeFile(outPath, r.out);
  } else {
    std::string fmt = toString(f.emit);
    std::vector<std::string> argv =
        {sc, "emit", "-t", *f.target, "-f", fmt, "-O", std::to_string(f.optimization_level)};
    std::string emittedInput=in;
    if(f.numerical_report||f.logical_ir_output){
      std::vector<std::string> command{sc,"compile","-t",*f.target,"-O",std::to_string(f.optimization_level)};
      if(f.numerical_report)command.insert(command.end(),{"--numerical-report",*f.numerical_report});
      if(f.logical_ir_output)command.insert(command.end(),{"--logical-ir-output",*f.logical_ir_output});
      command.push_back(in);auto compiled=runProc(command);
      if(compiled.rc!=0){std::cerr<<compiled.out;return compiled.rc;}
      emittedInput=(fs::temp_directory_path()/("qstack-native-"+std::to_string(_qs_pid())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".spinor")).string();
      writeFile(emittedInput,compiled.out);argv.emplace_back("--compiled");
    }
    if (f.verbatim) argv.emplace_back("--verbatim");
    argv.emplace_back(emittedInput);
    auto r = runProc(argv);
    if(emittedInput!=in){std::error_code ignored;fs::remove(emittedInput,ignored);}
    if (r.rc != 0) { std::cerr << r.out; return r.rc; }
    if (outPath.empty()) std::cout << r.out;
    else writeFile(outPath, r.out);
  }

  if (f.write_manifest && !outPath.empty()) {
    Manifest m;
    fs::path src = f.positionals[0];
    auto ext = src.extension().string();
    m.source_lang = (ext == ".phonon") ? "phonon"
                  : (ext == ".spn" || ext == ".spinor") ? "spinor"
                                                       : "unknown";
    m.source_path   = src.string();
    m.source_sha256 = sha256OfFile(src.string());
    m.target        = *f.target;
    auto prov = providerForChip(defaultRegistryRoot().string(), *f.target);
    m.provider      = f.provider.value_or(prov.value_or("unknown"));
    m.verbatim      = f.verbatim;
    m.shots         = f.shots.value_or(1024);
    m.produced_by_binary  = "phononc";
    m.produced_by_version = PHONONC_VERSION;
    m.produced_by_git_sha = "";
    m.produced_at_utc     = nowUtcIso8601();
    fs::path manPath = outPath + ".manifest.json";
    writeFile(manPath, toJson(m));
    if (!f.quiet) std::cerr << "wrote " << manPath.string() << "\n";
  }
  return 0;
}

int cmdEstimate(const Flags& f) {
  if (!f.target) { std::cerr << "estimate: --target required\n"; return 2; }
  if (f.positionals.empty()) {
    std::cerr << "estimate: missing input file\n"; return 2;
  }
  auto sc = findSpinorc();
  auto in = prepareSpinorFile(f.positionals[0], *f.target);
  auto r = runProc({sc, "check", "-t", *f.target, "-O", std::to_string(f.optimization_level), in});
  std::cout << r.out;
  return r.rc;
}

std::string resolveProvider(const Flags& f) {
  if (f.provider) return *f.provider;
  if (f.target) {
    auto p = providerForChip(defaultRegistryRoot().string(), *f.target);
    if (p) return *p;
  }
  return "ibm";
}

int cmdSubmitWithProgramName(const Flags& f, const std::string& overrideName);

int cmdSubmit(const Flags& f) {
  return cmdSubmitWithProgramName(f, std::string());
}

int cmdSubmitWithProgramName(const Flags& f, const std::string& overrideName) {
  if (!f.target) { std::cerr << "submit: --target required\n"; return 2; }
  if (f.positionals.empty()) {
    std::cerr << "submit: missing QASM3 file\n"; return 2;
  }
  SubmitRequest r;
  r.qasm_path     = f.positionals[0];
  r.chip          = *f.target;
  r.provider      = resolveProvider(f);
  r.shots         = f.shots.value_or(1024);
  r.mode          = f.mode;
  r.api_key_file  = f.api_key_file;
  r.api_key_stdin = f.api_key_stdin;
  r.url           = f.url;
  r.region        = f.region;
  r.instance      = f.instance;
  r.extras        = f.extras;
  r.verbose       = f.verbose;
  if (!overrideName.empty()) {
    r.program_name = overrideName;
  } else {
    r.program_name = fs::path(r.qasm_path).stem().string();
    if (r.program_name.empty()) r.program_name = "bell";
  }

  auto res = runPython(r);
  std::cout << res.stdout_text;
  if (!res.ok) {
    std::cerr << res.stderr_text;
    return res.exit_code == 0 ? 1 : res.exit_code;
  }
  return 0;
}

int cmdRun(const Flags& f) {
  if (!f.target) { std::cerr << "run: --target required\n"; return 2; }
  Flags compileF = f;
  fs::path qasmTmp = fs::temp_directory_path() /
      (std::string("phononc_run_") + std::to_string(_qs_pid()) + ".qasm3");
  compileF.output_path = qasmTmp.string();
  compileF.emit        = EmitFormat::Qasm3;
  int rc = cmdCompile(compileF);
  if (rc != 0) return rc;

  Flags submitF = f;
  submitF.positionals = {qasmTmp.string()};
  submitF.write_manifest = false;
  return cmdSubmitWithProgramName(
      submitF,
      fs::path(f.positionals[0]).stem().string());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && (std::string_view(argv[1]) == "run" ||
                   std::string_view(argv[1]) == "submit")) {
    std::vector<std::string> args(argv + 1, argv + argc);
    auto result = runQstack(args);
    std::cout << result.stdout_text;
    std::cerr << result.stderr_text;
    return result.exit_code;
  }
  auto f = parseArgv(argc, argv);
  if (!f.errors.empty()) {
    for (const auto& e : f.errors) std::cerr << "phononc: " << e << "\n";
    return 2;
  }
  if (f.subcommand == Subcommand::Help) {
    std::cout << renderHelp(kProgName, kShortDesc);
    return 0;
  }
  if (f.subcommand == Subcommand::Version) return cmdVersion(f);
  if (f.subcommand == Subcommand::Targets) return cmdTargets(f);
  if (f.subcommand == Subcommand::Providers) return cmdProviders(f);

  auto vErrs = validate(f);
  if (!vErrs.empty()) {
    for (const auto& e : vErrs) std::cerr << "phononc: " << e << "\n";
    return 2;
  }

  switch (f.subcommand) {
    case Subcommand::Compile:  return cmdCompile(f);
    case Subcommand::Estimate: return cmdEstimate(f);
    case Subcommand::Submit:   return cmdSubmit(f);
    case Subcommand::Run:      return cmdRun(f);
    default: break;
  }
  std::cerr << "phononc: unimplemented subcommand: " << toString(f.subcommand)
            << "\n";
  return 2;
}
