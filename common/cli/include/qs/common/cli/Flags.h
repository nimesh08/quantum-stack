// common/cli/include/qs/common/cli/Flags.h
//
// Declarative flag table + tiny argv parser shared by spinorc,
// phononc, and photonc.
//
// Design rule (open-cli-collective working-with-secrets standard):
// secret-bearing flags are NEVER accepted as bare CLI args. Only
// `--api-key-file <path>`, `--api-key-stdin`, or env vars. The parser
// rejects `--api-key=<literal>` and prints a precise diagnostic
// pointing at the right alternative.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qs::common::cli {

// The subcommand the user invoked.
enum class Subcommand {
  Compile,    // source -> compiled artifact
  Estimate,   // source -> resource estimate
  Submit,     // QASM3 file -> provider
  Run,        // source -> compile + submit
  Targets,    // list registry chips
  Providers,  // list providers + their flag conventions
  Version,    // print version + git sha
  Help,       // print --help and exit 0
  Unknown,
};

std::string toString(Subcommand s);
Subcommand parseSubcommand(std::string_view word);

// Submit / run mode: the same env var SPINOR_SUBMIT_MODE the existing
// Python adapter uses.
enum class Mode { Cassette, Live, Local };
std::string toString(Mode m);
std::optional<Mode> parseMode(std::string_view word);

// Output format the user selected for `compile`.
enum class EmitFormat { Qasm3, Qir, Quil, Phonon, Spinor };
std::string toString(EmitFormat f);
std::optional<EmitFormat> parseEmitFormat(std::string_view word);

// Parsed view of argv. Only fields the caller asked for are
// populated; everything else stays at default. The parser does NOT
// resolve secrets - that's the binary's job (see resolveApiKey() in
// Submit.h) - so we keep this struct dependency-free.
struct Flags {
  // Subcommand + positional inputs (the source file or QASM3 file).
  Subcommand subcommand = Subcommand::Unknown;
  std::vector<std::string> positionals;

  // Universal options.
  std::optional<std::string> target;
  std::optional<std::string> provider;
  std::optional<std::string> url;
  std::optional<std::string> region;
  std::optional<std::string> instance;
  std::optional<std::string> api_key_file;
  bool                         api_key_stdin = false;
  std::optional<std::string> config_file;

  std::optional<int>           shots;
  std::optional<double>        cost_cap_usd;

  Mode                         mode = Mode::Cassette;
  EmitFormat                   emit = EmitFormat::Qasm3;
  bool                         verbatim = true;
  std::optional<std::string> output_path;
  bool                         write_manifest = false;
  bool                         verbose = false;
  bool                         quiet   = false;

  // Provider-specific escape hatch (--extra k=v, repeatable).
  std::map<std::string, std::string> extras;

  // Diagnostic info.
  std::vector<std::string>     errors;     // parse errors -> exit 2
  std::vector<std::string>     warnings;
};

// Parse argv. argc must be >= 1 (program name in argv[0]).
// On any error, `Flags::errors` is non-empty and the caller exits 2
// after printing them.
Flags parseArgv(int argc, const char* const* argv);

// Render the unified --help text. Programs print this with their own
// program name and short description prepended.
std::string renderHelp(std::string_view programName,
                       std::string_view shortDesc);

// Validate that every required field for the given subcommand is
// populated. Returns errors as strings; empty vector = OK.
std::vector<std::string> validate(const Flags& f);

}  // namespace qs::common::cli
