// photon/bindings/lib/Engine.cpp

#include "photon/bindings/Engine.h"

#include "phonon/lower/Lowering.h"
#include "phonon/optimizer/Pipeline.h"
#include "phonon/parser/Parser.h"
#include "phonon/types/LinearTypeChecker.h"
#include "spinor/dialect/Spinor.h"

#include <sstream>

namespace photon::bindings {
namespace pd = phonon::dialect;
namespace sd = spinor::dialect;

CompiledProgram CompiledProgram::fromPhononText(std::string_view phn,
                                                std::string_view target) {
  CompiledProgram cp;
  auto pr = phonon::parser::parse(phn, "<binding>");
  if (!pr.module) {
    std::ostringstream os;
    os << "parse failed";
    for (const auto& d : pr.diag.items()) os << "\n  " << d.message;
    cp.error_ = os.str();
    return cp;
  }
  return fromPhononModule(std::move(*pr.module), target);
}

CompiledProgram CompiledProgram::fromPhononModule(pd::Module mod,
                                                  std::string_view target) {
  CompiledProgram cp;
  cp.phn_ = std::move(mod);
  if (!target.empty()) cp.phn_.targetAttr = std::string(target);

  // Linear typecheck (no target_info available at the binding level —
  // we use the default chip-agnostic options).
  pd::Diagnostics diag;
  phonon::types::Options opt;
  opt.midCircuitMeasure = true;
  if (!phonon::types::typecheck(cp.phn_, opt, diag)) {
    std::ostringstream os;
    os << "typecheck failed";
    for (const auto& d : diag.items()) os << "\n  " << d.message;
    cp.error_ = os.str();
    return cp;
  }

  // Optimize-once pipeline (NullImpls for borrowed passes).
  phonon::optimizer::PipelineConfig cfg;
  (void)phonon::optimizer::runPipeline(cp.phn_, std::move(cfg));

  // Lower to flat Spinor.
  auto lr = phonon::lower::lower(cp.phn_, /*target=*/nullptr);
  if (lr.module) cp.spn_ = std::move(*lr.module);

  cp.ok_ = true;
  return cp;
}

std::string CompiledProgram::dumpPhonon() const {
  if (!ok_) return {};
  return pd::print(phn_);
}

std::string CompiledProgram::dumpSpinor() const {
  if (!spn_) return {};
  return sd::print(*spn_);
}

ResourceEstimate CompiledProgram::estimate() const {
  ResourceEstimate r;
  if (!spn_) return r;
  for (const auto& op : spn_->ops()) {
    auto k = op.kind;
    using OK = sd::OpKind;
    switch (k) {
      case OK::AllocQubit: ++r.num_qubits; break;
      case OK::Cx: case OK::Cz: case OK::Swap:
      case OK::Ecr: case OK::Ms: case OK::Rzz:
        ++r.two_qubit_count; break;
      case OK::T: case OK::Tdg: ++r.t_count; break;
      default: break;
    }
  }
  // Linear depth proxy: number of non-allocation ops in sequence.
  std::size_t d = 0;
  for (const auto& op : spn_->ops()) {
    if (op.kind != sd::OpKind::AllocQubit && op.kind != sd::OpKind::AllocBit)
      ++d;
  }
  r.depth = d;
  return r;
}

}  // namespace photon::bindings
