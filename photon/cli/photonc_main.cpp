// photon/cli/photonc_main.cpp
//
// `photonc` - the top-of-stack driver for the Photon language.
//
// Pipeline:
//   .pho     -> photon_lang::parse + lowerToPhonon  -> phonon text
//   .phonon  -> (passthrough)                       -> phonon text
//   .spinor  -> (passthrough)                       -> spinor text
//
// Then for `compile / estimate / run`, we shell out to `spinorc`
// for placement / routing / decompose / emit / check (gcc-style:
// the top driver invokes the lower binary as a subprocess).
//
// For `submit / run`, we shell out to `python -m spinor_submit` via
// the shared common::cli::runPython helper.

#include "photon/lang/Lower.h"
#include "photon/lang/Module.h"
#include "photon/lang/Parser.h"
#include "phonon/dialect/Phonon.h"
#include "phonon/optimizer/Pipeline.h"
#include "qs/common/cli/Flags.h"
#include "qs/common/cli/Manifest.h"
#include "qs/common/cli/Providers.h"
#include "qs/common/cli/Submit.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#  include <process.h>
#  define _qs_pid() static_cast<long>(::_getpid())
#else
#  include <unistd.h>
#  define _qs_pid() static_cast<long>(::getpid())
#endif

#ifndef PHOTONC_VERSION
#  define PHOTONC_VERSION "0.1.0"
#endif

namespace fs = std::filesystem;
using namespace qs::common::cli;

namespace {

constexpr const char* kProgName = "photonc";
constexpr const char* kShortDesc =
    "Photon language driver (top of the Heisenberg stack)";

std::string slurp(const fs::path& p) {
  std::ifstream f(p);
  if (!f) {
    std::cerr << "photonc: cannot read " << p.string() << "\n";
    std::exit(1);
  }
  std::ostringstream s; s << f.rdbuf();
  return s.str();
}

void writeFile(const fs::path& p, std::string_view text) {
  std::ofstream f(p);
  f << text;
}

void dumpDiagnostics(const photon::lang::Diagnostics& d) {
  for (const auto& di : d.items()) {
    const char* sev =
        (di.severity == photon::lang::DiagSeverity::Error) ? "error"
                                                           : "warning";
    std::cerr << sev;
    if (!di.loc.file.empty() || di.loc.line > 0) {
      std::cerr << " " << di.loc.file << ":" << di.loc.line << ":"
                << di.loc.column;
    }
    std::cerr << ": " << di.message << "\n";
  }
}

// Detect the input language by extension.
enum class InputLang { Pho, Phonon, Spinor, Unknown };
InputLang detectInput(const fs::path& p) {
  auto ext = p.extension().string();
  if (ext == ".pho")     return InputLang::Pho;
  if (ext == ".phonon")  return InputLang::Phonon;
  if (ext == ".spn" || ext == ".spinor") return InputLang::Spinor;
  return InputLang::Unknown;
}

fs::path defaultRegistryRoot() {
  if (const char* p = std::getenv("SPINOR_REGISTRY_ROOT")) {
    return p;
  }
  // Source-tree default for development. For installed binaries, the
  // env var must be set.
  return "spinor/registry";
}

// Resolve the location of `spinorc` so the driver can shell out.
// Honours $QSTACK_SPINORC, then PATH, then the source-tree build dir.
std::string findSpinorc() {
  if (const char* p = std::getenv("QSTACK_SPINORC")) {
    if (fs::exists(p)) return p;
  }
  // Try $PATH.
  const char* path = std::getenv("PATH");
  if (path) {
    std::string s = path;
    std::string token;
    std::stringstream ss(s);
    while (std::getline(ss, token, ':')) {
      fs::path cand = fs::path(token) / "spinorc";
      if (fs::exists(cand)) return cand.string();
    }
  }
  fs::path here = "build/spinor/cli/spinorc";
  if (fs::exists(here)) return fs::absolute(here).string();
  return "spinorc";  // hope for the best; shells out via PATH lookup
}

// Run a subprocess and capture stdout/stderr. Returns exit code.
struct ProcResult { int rc = 0; std::string out, err; };
ProcResult runProc(const std::vector<std::string>& argv) {
  // Reuse the same approach as common/cli/Submit.cpp via popen on
  // Windows, fork+exec on POSIX. To keep this file small we use the
  // existing helper indirectly.
  // For simplicity, we just merge stderr into the parent's stderr
  // (no capture) and capture stdout via popen.
  std::ostringstream cmd;
  for (size_t i = 0; i < argv.size(); ++i) {
    if (i) cmd << ' ';
    cmd << '"' << argv[i] << '"';
  }
  ProcResult res;
  // Stream stderr to parent's stderr so users see compile errors live.
  std::string full = cmd.str() + " 2>&1";
  FILE* p = ::popen(full.c_str(), "r");
  if (!p) {
    res.rc = 127;
    res.err = "popen failed for: " + cmd.str();
    return res;
  }
  char buf[4096];
  while (std::fgets(buf, sizeof(buf), p)) {
    res.out += buf;
  }
  int rc = ::pclose(p);
  // pclose returns the wait status on POSIX; extract the exit code.
#ifdef WEXITSTATUS
  res.rc = WIFEXITED(rc) ? WEXITSTATUS(rc) : rc;
#else
  res.rc = rc;
#endif
  return res;
}

// Walk a lowered Phonon module and emit it in the simple spinor-text
// form that `spinorc` accepts:
//
//   target <chip>
//   qubit q[N]
//   bit __c_q[N]
//   <gate> q[i]
//   <gate> q[i], q[j]
//   __c_q[i] = measure q[i]
//
// This sits alongside (not on top of) phonon::dialect::print(), which
// emits the MLIR-style textual form used by the engine and tests.
std::string emitSpinorText(const phonon::dialect::Module& m) {
  using OK = phonon::dialect::OpKind;
  std::ostringstream os;
  os << "target " << (m.targetAttr.empty() ? "generic" : m.targetAttr) << "\n";

  // Map ValueId -> qubit index. Each AllocQubit op produces a result
  // value; we assign sequential indices as we encounter them.
  // Use std::map (ValueId has operator<) since ValueId is not hashable.
  std::map<phonon::dialect::ValueId, int> qIdx;
  int nextQubit = 0;
  int bitCount = 0;

  // First pass: count qubits + bits to emit the declarations.
  for (const auto& op : m.ops()) {
    if (op.kind == OK::AllocQubit) {
      qIdx[op.results.front()] = nextQubit++;
    } else if (op.kind == OK::AllocBit) {
      ++bitCount;
    }
  }
  if (nextQubit > 0) os << "qubit q[" << nextQubit << "]\n";
  if (bitCount  > 0) os << "bit __c_q[" << bitCount << "]\n";

  // Second pass: emit gates / measurements. Track value forwarding:
  // a gate op's operand may be a result of a previous gate op (SSA),
  // but the qubit slot is the same. Build a value->slot resolver that
  // chases through prior gate results back to the original AllocQubit.
  std::map<phonon::dialect::ValueId, int> slotOf = qIdx;
  // The allocator above only mapped AllocQubit results. As we walk
  // gate ops, each result that flows from a qubit operand inherits
  // its slot. We process ops in order; for any gate op, the i-th
  // qubit result inherits the i-th qubit operand's slot.
  auto resolveSlot = [&](phonon::dialect::ValueId v) -> int {
    auto it = slotOf.find(v);
    return (it == slotOf.end()) ? -1 : it->second;
  };

  int measured = 0;
  for (const auto& op : m.ops()) {
    if (op.kind == OK::AllocQubit || op.kind == OK::AllocBit) continue;
    if (op.kind == OK::Measure) {
      int s = resolveSlot(op.operands.front());
      if (s < 0) continue;
      os << "__c_q[" << measured << "] = measure q[" << s << "]\n";
      ++measured;
      continue;
    }
    auto mn = phonon::dialect::opMnemonic(op.kind);
    // mnemonic includes the dialect prefix (e.g. "spinor.h"); strip it.
    std::string name(mn);
    auto dot = name.find('.');
    if (dot != std::string::npos) name = name.substr(dot + 1);
    int qa = phonon::dialect::qubitArity(op.kind);
    if (qa == 0) continue;  // skip non-quantum (constants etc.)
    if (qa == 1) {
      int s = resolveSlot(op.operands.front());
      if (s < 0) continue;
      os << name << " q[" << s << "]\n";
      // The op's qubit result inherits the slot.
      if (!op.results.empty()) slotOf[op.results.front()] = s;
    } else if (qa == 2) {
      int sa = resolveSlot(op.operands[0]);
      int sb = resolveSlot(op.operands[1]);
      if (sa < 0 || sb < 0) continue;
      os << name << " q[" << sa << "], q[" << sb << "]\n";
      if (op.results.size() >= 2) {
        slotOf[op.results[0]] = sa;
        slotOf[op.results[1]] = sb;
      }
    }
  }
  return os.str();
}

// Read source, lower to phonon module, run the Phonon optimizer
// (cancellation + rotation merging + commutation + null borrowed
// passes by default; matches what photon::bindings::CompiledProgram
// does for the @photon.kernel and [[photon::kernel]] paths), then
// print the optimized module as spinor text.
//
// Always uses target=ibm_heron_r2 as a safe default for the photon
// parser (it's just a stamp that the lowering writes into the printed
// header; the chip is re-read by spinorc downstream).
std::string lowerPhotonToSpinorText(const std::string& src,
                                    std::string_view filename,
                                    std::string_view target) {
  auto pr = photon::lang::parse(src, filename);
  if (!pr.module) {
    dumpDiagnostics(pr.diag);
    std::exit(1);
  }
  pr.module->target = std::string(target);
  auto lr = photon::lang::lowerToPhonon(*pr.module);
  if (!lr.module) {
    dumpDiagnostics(lr.diag);
    std::exit(1);
  }
  // The Phonon optimizer pipeline. NullImpls are used for the
  // borrowed adapters (Tweedledum / PyZX) by default. To opt in to
  // PyZX, set PHONON_PYZX_LIVE=1 and ensure pyzx is on the include
  // path; that decision is the caller's, not photonc's.
  phonon::optimizer::PipelineConfig cfg;
  (void)phonon::optimizer::runPipeline(*lr.module, std::move(cfg));
  return emitSpinorText(*lr.module);
}

// For .phonon and .spinor inputs, just slurp the file contents.
std::string asPhononOrSpinorText(const fs::path& path, InputLang lang) {
  std::string body = slurp(path);
  // .phonon and .spinor share a parser-friendly shape; spinorc reads
  // both. Nothing to do.
  (void)lang;
  return body;
}

int cmdTargets(const Flags&) {
  // Defer to `spinorc registry list`.
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
  std::cout << kProgName << " " << PHOTONC_VERSION << "\n";
  std::cout << "engine: photon_lang + phonon_dialect (in-process)\n";
  std::cout << "lowering subprocess: " << findSpinorc() << "\n";
  return 0;
}

// Prepare a Phonon-text file from any of the three input languages.
// Returns the path of a temporary .phonon file the caller can hand to
// spinorc (or a path to the input file if it's already phonon/spinor).
std::string prepareLoweredFile(const Flags& f, std::string_view target) {
  if (f.positionals.empty()) {
    std::cerr << "photonc: missing input file\n";
    std::exit(2);
  }
  fs::path in = f.positionals[0];
  auto lang = detectInput(in);
  if (lang == InputLang::Pho) {
    auto txt = lowerPhotonToSpinorText(slurp(in), in.string(), target);
    fs::path tmp = fs::temp_directory_path() /
        (std::string("photonc_") + std::to_string(_qs_pid()) + ".phonon");
    writeFile(tmp, txt);
    return tmp.string();
  }
  if (lang == InputLang::Phonon || lang == InputLang::Spinor) {
    return in.string();
  }
  std::cerr << "photonc: cannot detect input language for "
            << in.string() << " (expected .pho / .phonon / .spinor / .spn)\n";
  std::exit(2);
}

int cmdCompile(const Flags& f) {
  if (!f.target) { std::cerr << "compile: --target required\n"; return 2; }
  auto lowered = prepareLoweredFile(f, *f.target);

  // For --emit phonon and --emit spinor we just dump the lowered
  // file's content (after running it through spinorc compile so the
  // spinor case is fully decomposed for the chip).
  std::string outPath = f.output_path.value_or("");
  auto sc = findSpinorc();

  if (f.emit == EmitFormat::Phonon) {
    // The lowered text already IS Phonon. Print or save it.
    std::string body = slurp(lowered);
    if (outPath.empty()) std::cout << body;
    else writeFile(outPath, body);
  } else if (f.emit == EmitFormat::Spinor) {
    // Run `spinorc compile -t <chip> <lowered>` to get spinor text.
    auto r = runProc({sc, "compile", "-t", *f.target, lowered});
    if (r.rc != 0) { std::cerr << r.out; return r.rc; }
    if (outPath.empty()) std::cout << r.out;
    else writeFile(outPath, r.out);
  } else {
    // qasm3 / qir / quil emit through spinorc emit.
    std::string fmt = (f.emit == EmitFormat::Qasm3) ? "qasm3"
                    : (f.emit == EmitFormat::Qir)   ? "qir"
                                                    : "quil";
    std::vector<std::string> argv =
        {sc, "emit", "-t", *f.target, "-f", fmt};
    if (f.verbatim) argv.emplace_back("--verbatim");
    argv.emplace_back(lowered);
    auto r = runProc(argv);
    if (r.rc != 0) { std::cerr << r.out; return r.rc; }
    if (outPath.empty()) std::cout << r.out;
    else writeFile(outPath, r.out);
  }

  // --manifest writes a sidecar JSON next to the artifact.
  if (f.write_manifest && !outPath.empty()) {
    Manifest m;
    fs::path src = f.positionals[0];
    auto lang = detectInput(src);
    m.source_lang =
        lang == InputLang::Pho     ? "photon" :
        lang == InputLang::Phonon  ? "phonon" :
        lang == InputLang::Spinor  ? "spinor" : "unknown";
    m.source_path = src.string();
    m.source_sha256 = sha256OfFile(src.string());
    m.target = *f.target;
    auto prov = providerForChip(defaultRegistryRoot().string(), *f.target);
    m.provider = f.provider.value_or(prov.value_or("unknown"));
    m.verbatim = f.verbatim;
    m.shots = f.shots.value_or(1024);
    m.produced_by_binary  = "photonc";
    m.produced_by_version = PHOTONC_VERSION;
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
  auto lowered = prepareLoweredFile(f, *f.target);
  auto sc = findSpinorc();
  auto r = runProc({sc, "check", "-t", *f.target, lowered});
  std::cout << r.out;
  return r.rc;
}

// Resolve provider given chip; auto-derive from registry if not set.
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
  // compile to a temporary QASM3 file, then submit.
  if (!f.target) { std::cerr << "run: --target required\n"; return 2; }
  Flags compileF = f;
  fs::path qasmTmp = fs::temp_directory_path() /
      (std::string("photonc_run_") + std::to_string(_qs_pid()) + ".qasm3");
  compileF.output_path = qasmTmp.string();
  compileF.emit        = EmitFormat::Qasm3;
  int rc = cmdCompile(compileF);
  if (rc != 0) return rc;

  // Now submit the compiled QASM3. Cassette lookup uses the *input
  // file's* stem (not the temp file's name) so re-running the same
  // source replays the same cassette.
  Flags submitF = f;
  submitF.positionals = {qasmTmp.string()};
  submitF.write_manifest = false;
  return cmdSubmitWithProgramName(
      submitF,
      fs::path(f.positionals[0]).stem().string());
}

}  // namespace

int main(int argc, char** argv) {
  auto f = parseArgv(argc, argv);
  if (!f.errors.empty()) {
    for (const auto& e : f.errors) std::cerr << "photonc: " << e << "\n";
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
    for (const auto& e : vErrs) std::cerr << "photonc: " << e << "\n";
    return 2;
  }

  switch (f.subcommand) {
    case Subcommand::Compile:  return cmdCompile(f);
    case Subcommand::Estimate: return cmdEstimate(f);
    case Subcommand::Submit:   return cmdSubmit(f);
    case Subcommand::Run:      return cmdRun(f);
    default: break;
  }
  std::cerr << "photonc: unimplemented subcommand: " << toString(f.subcommand)
            << "\n";
  return 2;
}
