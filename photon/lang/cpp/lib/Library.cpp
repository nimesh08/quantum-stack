// photon/lang/cpp/lib/Library.cpp
//
// Library expanders. Catalogue documented in
// docs/build/phaseC/M2_lib.md §5.

#include "photon/lang/Library.h"

#include <cmath>
#include <numbers>
#include <unordered_set>

namespace photon::lang {
namespace pd = phonon::dialect;

namespace {

const std::unordered_set<std::string>& routines() {
  static const std::unordered_set<std::string> r{
    "bell_pair", "ghz", "qft", "iqft",
    "grover", "teleport", "vqe_ansatz",
  };
  return r;
}

std::vector<pd::ValueId>* slotsOf(ExpandCtx& ctx,
                                  const std::string& name) {
  auto it = ctx.qslots->find(name);
  if (it == ctx.qslots->end()) return nullptr;
  return &it->second;
}

void emitErr(ExpandCtx& ctx, std::string msg, const Location& loc) {
  if (ctx.diag) ctx.diag->error(std::move(msg),
                                {loc.file, loc.line, loc.column});
}

// Helper: apply h to slot index i (slot table updated).
void applyH(std::vector<pd::ValueId>& slots, std::size_t i,
            pd::Builder& b, pd::Location L) {
  slots[i] = b.h(slots[i], L);
}
void applyX(std::vector<pd::ValueId>& slots, std::size_t i,
            pd::Builder& b, pd::Location L) {
  slots[i] = b.x(slots[i], L);
}
void applyZ(std::vector<pd::ValueId>& slots, std::size_t i,
            pd::Builder& b, pd::Location L) {
  slots[i] = b.z(slots[i], L);
}
void applyRz(std::vector<pd::ValueId>& slots, std::size_t i, double angle,
             pd::Builder& b, pd::Location L) {
  slots[i] = b.rz(angle, slots[i], L);
}
void applyRy(std::vector<pd::ValueId>& slots, std::size_t i, double angle,
             pd::Builder& b, pd::Location L) {
  slots[i] = b.ry(angle, slots[i], L);
}
void applyCx(std::vector<pd::ValueId>& slots, std::size_t a, std::size_t b_,
             pd::Builder& b, pd::Location L) {
  auto r = b.cx(slots[a], slots[b_], L);
  slots[a] = r.first;
  slots[b_] = r.second;
}
void applySwap(std::vector<pd::ValueId>& slots, std::size_t a, std::size_t b_,
               pd::Builder& b, pd::Location L) {
  auto r = b.swap(slots[a], slots[b_], L);
  slots[a] = r.first;
  slots[b_] = r.second;
}

// Exact controlled phase diag(1, 1, 1, exp(i theta)), as required by QFT.
// The Rz/CX sequence alone contributes an unwanted scalar exp(-i theta/4).
void applyCRz(std::vector<pd::ValueId>& slots, std::size_t c, std::size_t t,
              double theta, pd::Builder& b, pd::Location L) {
  applyRz(slots, c, theta / 2.0, b, L);
  applyCx(slots, c, t, b, L);
  applyRz(slots, t, -theta / 2.0, b, L);
  applyCx(slots, c, t, b, L);
  applyRz(slots, t, theta / 2.0, b, L);
  b.globalPhase(theta / 4.0, L);
}

// --- bell_pair(q, a, b) ---------------------------------------------------
bool expandBellPair(const std::string& recv,
                    const std::vector<ExprPtr>& args,
                    const Location& loc, ExpandCtx& ctx) {
  std::size_t aIdx = 0, bIdx = 1;
  if (args.size() >= 2) {
    auto a = ctx.foldInt(args[0]);
    auto b_ = ctx.foldInt(args[1]);
    if (!a || !b_) {
      emitErr(ctx, "bell_pair: indices must fold at compile time", loc);
      return false;
    }
    aIdx = *a; bIdx = *b_;
  }
  auto* slots = slotsOf(ctx, recv);
  if (!slots) { emitErr(ctx, "bell_pair: unknown qreg '" + recv + "'", loc); return false; }
  if (aIdx >= slots->size() || bIdx >= slots->size() || aIdx == bIdx) {
    emitErr(ctx, "bell_pair requires two distinct in-range qubits", loc); return false;
  }
  pd::Location L = ctx.loc;
  applyH(*slots, aIdx, *ctx.builder, L);
  applyCx(*slots, aIdx, bIdx, *ctx.builder, L);
  return true;
}

// --- ghz(q) ---------------------------------------------------------------
bool expandGhz(const std::string& recv, const std::vector<ExprPtr>&,
               const Location& loc, ExpandCtx& ctx) {
  auto* slots = slotsOf(ctx, recv);
  if (!slots) { emitErr(ctx, "ghz: unknown qreg '" + recv + "'", loc); return false; }
  if (slots->empty()) { emitErr(ctx, "ghz: empty qreg", loc); return false; }
  applyH(*slots, 0, *ctx.builder, ctx.loc);
  for (std::size_t i = 0; i + 1 < slots->size(); ++i) {
    applyCx(*slots, i, i + 1, *ctx.builder, ctx.loc);
  }
  return true;
}

// --- qft(q) ---------------------------------------------------------------
bool expandQft(const std::string& recv, const std::vector<ExprPtr>&,
               const Location& loc, ExpandCtx& ctx) {
  auto* slots = slotsOf(ctx, recv);
  if (!slots) { emitErr(ctx, "qft: unknown qreg '" + recv + "'", loc); return false; }
  std::size_t n = slots->size();
  for (std::size_t j = 0; j < n; ++j) {
    applyH(*slots, j, *ctx.builder, ctx.loc);
    for (std::size_t k = j + 1; k < n; ++k) {
      double theta = std::numbers::pi / std::pow(2.0, static_cast<double>(k - j));
      applyCRz(*slots, k, j, theta, *ctx.builder, ctx.loc);
    }
  }
  for (std::size_t i = 0; i < n / 2; ++i) {
    applySwap(*slots, i, n - 1 - i, *ctx.builder, ctx.loc);
  }
  return true;
}

bool expandIqft(const std::string& recv, const std::vector<ExprPtr>&,
                const Location& loc, ExpandCtx& ctx) {
  auto* slots = slotsOf(ctx, recv);
  if (!slots) { emitErr(ctx, "iqft: unknown qreg '" + recv + "'", loc); return false; }
  std::size_t n = slots->size();
  for (std::size_t i = 0; i < n / 2; ++i) {
    applySwap(*slots, i, n - 1 - i, *ctx.builder, ctx.loc);
  }
  for (std::size_t j = n; j-- > 0;) {
    for (std::size_t k = n; k-- > j + 1;) {
      double theta = -std::numbers::pi / std::pow(2.0, static_cast<double>(k - j));
      applyCRz(*slots, k, j, theta, *ctx.builder, ctx.loc);
    }
    applyH(*slots, j, *ctx.builder, ctx.loc);
  }
  return true;
}

// Grover needs a bound oracle and exact multi-controlled diffusion. The
// language does not represent that callable contract yet; never replace
// either operation with a placeholder or a different diagonal circuit.
bool expandGrover(const std::string&, const std::vector<ExprPtr>&,
                  const Location& loc, ExpandCtx& ctx) {
  emitErr(ctx, "grover requires a bound oracle and exact diffusion; callable oracle lowering is unsupported", loc);
  return false;
}

// --- teleport(q, src, anc, dst) ------------------------------------------
bool expandTeleport(const std::string& recv,
                    const std::vector<ExprPtr>& args,
                    const Location& loc, ExpandCtx& ctx) {
  auto* slots = slotsOf(ctx, recv);
  if (!slots) { emitErr(ctx, "teleport: unknown qreg", loc); return false; }
  std::size_t src = 0, anc = 1, dst = 2;
  if (args.size() >= 3) {
    auto a = ctx.foldInt(args[0]); auto b_ = ctx.foldInt(args[1]);
    auto c = ctx.foldInt(args[2]);
    if (!a || !b_ || !c) {
      emitErr(ctx, "teleport: slot indices must fold", loc); return false;
    }
    src = *a; anc = *b_; dst = *c;
  }
  if (src >= slots->size() || anc >= slots->size() || dst >= slots->size() ||
      src == anc || src == dst || anc == dst) {
    emitErr(ctx, "teleport requires three distinct in-range qubits", loc); return false;
  }
  pd::Builder& b = *ctx.builder;
  pd::Location L = ctx.loc;
  applyH(*slots, anc, b, L);
  applyCx(*slots, anc, dst, b, L);
  applyCx(*slots, src, anc, b, L);
  applyH(*slots, src, b, L);
  // Measurements + classical-controlled corrections.
  pd::ValueId m_src = b.measure((*slots)[src], L);
  pd::ValueId m_anc = b.measure((*slots)[anc], L);
  pd::ValueId one = b.constInt(1, L);
  pd::ValueId p1 = b.cmp("==", m_anc, one, L);
  auto if1 = b.beginIf(p1, L);
  applyX(*slots, dst, b, L);
  b.endIf(if1, L);
  pd::ValueId p2 = b.cmp("==", m_src, one, L);
  auto if2 = b.beginIf(p2, L);
  applyZ(*slots, dst, b, L);
  b.endIf(if2, L);
  return true;
}

// --- vqe_ansatz(q, depth) ------------------------------------------------
bool expandVqeAnsatz(const std::string& recv,
                     const std::vector<ExprPtr>& args,
                     const Location& loc, ExpandCtx& ctx) {
  auto* slots = slotsOf(ctx, recv);
  if (!slots) { emitErr(ctx, "vqe_ansatz: unknown qreg", loc); return false; }
  std::int64_t depth = 1;
  if (!args.empty()) {
    auto v = ctx.foldInt(args[0]);
    if (!v) { emitErr(ctx, "vqe_ansatz: depth must fold", loc); return false; }
    depth = *v;
  }
  std::size_t n = slots->size();
  if (depth < 1 || n == 0 || static_cast<std::uint64_t>(depth) > 100000 / n) {
    emitErr(ctx, "vqe_ansatz: depth must be positive and expansion must not exceed 100000 qubit-layers", loc);
    return false;
  }
  for (std::int64_t d = 0; d < depth; ++d) {
    for (std::size_t i = 0; i < n; ++i) {
      double theta = 0.1 * static_cast<double>(d * static_cast<std::int64_t>(n) +
                                               static_cast<std::int64_t>(i));
      applyRy(*slots, i, theta, *ctx.builder, ctx.loc);
    }
    for (std::size_t i = 0; i + 1 < n; ++i) {
      applyCx(*slots, i, i + 1, *ctx.builder, ctx.loc);
    }
  }
  return true;
}

}  // namespace

bool isLibraryRoutine(const std::string& name) {
  return routines().contains(name);
}

bool expandLibrary(const std::string& name,
                   const std::string& recv,
                   const std::vector<ExprPtr>& args,
                   const Location& loc,
                   ExpandCtx& ctx) {
  if (!isLibraryRoutine(name)) return false;
  if (name == "bell_pair" && args.size() != 0 && args.size() != 2) {
    emitErr(ctx, "bell_pair expects zero or two slot indices", loc); return false;
  }
  if (name == "teleport" && args.size() != 0 && args.size() != 3) {
    emitErr(ctx, "teleport expects zero or three slot indices", loc); return false;
  }
  if ((name == "ghz" || name == "qft" || name == "iqft") && !args.empty()) {
    emitErr(ctx, name + " expects no arguments", loc); return false;
  }
  if (name == "vqe_ansatz" && args.size() > 1) {
    emitErr(ctx, "vqe_ansatz expects zero or one depth argument", loc); return false;
  }
  if (name == "bell_pair")  return expandBellPair(recv, args, loc, ctx);
  if (name == "ghz")        return expandGhz(recv, args, loc, ctx);
  if (name == "qft")        return expandQft(recv, args, loc, ctx);
  if (name == "iqft")       return expandIqft(recv, args, loc, ctx);
  if (name == "grover")     return expandGrover(recv, args, loc, ctx);
  if (name == "teleport")   return expandTeleport(recv, args, loc, ctx);
  if (name == "vqe_ansatz") return expandVqeAnsatz(recv, args, loc, ctx);
  return false;
}

}  // namespace photon::lang
