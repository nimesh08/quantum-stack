#pragma once
#include "spinor/dialect/Circuit.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace spinor::passes {
// Observations of floating-point reconstructions, never certified bounds.
struct RewriteObservation {
  std::string pass, rule, region, inputDigest, outputDigest;
  std::size_t arity = 0;
  double phaseDelta = 0;
  // Null means an analytic identity was observed without a separate numeric
  // acceptance guard. It must not be presented as a checked threshold.
  std::optional<double> threshold, residual;
  std::string unmeasuredReason;
};
struct CompilationReport {
  static constexpr unsigned schemaVersion = 1;
  std::vector<RewriteObservation> rewrites;
  std::vector<std::string> gaps;
  std::map<std::string, std::size_t> counters;
  std::map<std::string, std::string> notes;
  // Describes the whole input; it does not invalidate observations within
  // separate unitary regions. Those sums remain diagnostics, never a bound
  // on an executed branch, quantum instrument or the complete program.
  bool hasNonunitaryOperations = false;
  std::string stage = "native";
  void addGap(const std::string& message);
  std::string json() const;
};
// Public numerical-evidence contract; preserve the original sink name for
// existing pass clients without introducing a second serialized schema.
using NumericalReport = CompilationReport;
// A scoped sink avoids changing every public pass signature. It is local to
// the compiling thread. Temporary synthesis candidates use a null scope.
CompilationReport* currentCompilationReport();
class CompilationReportScope {
  CompilationReport* previous_;
public:
  explicit CompilationReportScope(CompilationReport* report);
  ~CompilationReportScope();
  CompilationReportScope(const CompilationReportScope&) = delete;
  CompilationReportScope& operator=(const CompilationReportScope&) = delete;
};
void observeRewrite(const std::string& pass, const std::string& rule,
    const std::vector<dialect::WireOp>& before,
    const std::vector<dialect::WireOp>& after,
    double beforePhase = 0, double afterPhase = 0,
    std::optional<double> reconstructionThreshold = std::nullopt,
    const std::string& region = "unitary-0");
// Nonunitary, classical and unknown instructions conservatively separate
// optimization regions; global phase remains a unitary scalar operation.
bool isUnitaryInstruction(const dialect::WireOp&);
bool isNumericalRegionBoundary(const dialect::WireOp&);
std::string numericalRegion(std::size_t index);
}
