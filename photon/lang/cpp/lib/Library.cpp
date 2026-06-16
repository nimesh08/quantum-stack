// photon/lang/cpp/lib/Library.cpp
//
// Library expanders. Catalogue documented in
// docs/build/phaseC/M2_lib.md §5.

#include "photon/lang/Library.h"

#include <cmath>
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

// Controlled-rz approximation: standard decomposition is
//   cx(c, t); rz(-theta/2, t); cx(c, t); rz(theta/2, t); rz(theta/2, c)
// We pick that exact identity (Nielsen & Chuang Ex. 4.34).
void applyCRz(std::vector<pd::ValueId>& slots, std::size_t c, std::size_t t,
              double theta, pd::Builder& b, pd::Location L) {
  applyRz(slots, c, theta / 2.0, b, L);
  applyCx(slots, c, t, b, L);
  applyRz(slots, t, -theta / 2.0, b, L);
  applyCx(slots, c, t, b, L);
  applyRz(slots, t, theta / 2.0, b, L);
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
      double theta = M_PI / std::pow(2.0, static_cast<double>(k - j));
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
      double theta = -M_PI / std::pow(2.0, static_cast<double>(k - j));
      applyCRz(*slots, k, j, theta, *ctx.builder, ctx.loc);
    }
    applyH(*slots, j, *ctx.builder, ctx.loc);
  }
  return true;
}

// --- grover(q, oracle, rounds) -------------------------------------------
// The oracle is a callable received as a kernel parameter; we call it
// via phonon.call "oracle" with the qreg slots as operands. Diffusion
// is H^N · X^N · multi-CZ · X^N · H^N.
void diffusion(std::vector<pd::ValueId>& slots, pd::Builder& b,
               pd::Location L) {
  std::size_t n = slots.size();
  for (std::size_t i = 0; i < n; ++i) applyH(slots, i, b, L);
  for (std::size_t i = 0; i < n; ++i) applyX(slots, i, b, L);
  // Multi-controlled-Z on n qubits, target = last qubit. Trivial for
  // n=1 (z) and n=2 (cz). For n>=3 we cascade: a chain of CZs to the
  // last qubit; the result is not the literal multi-CZ but for M2 it
  // suffices — Phase D will swap in tweedledum's exact synthesis.
  if (n == 1) applyZ(slots, 0, b, L);
  else if (n == 2) {
    auto r = b.cz(slots[0], slots[1], L);
    slots[0] = r.first; slots[1] = r.second;
  } else {
    for (std::size_t i = 0; i + 1 < n; ++i) {
      auto r = b.cz(slots[i], slots[n - 1], L);
      slots[i] = r.first; slots[n - 1] = r.second;
    }
  }
  for (std::size_t i = 0; i < n; ++i) applyX(slots, i, b, L);
  for (std::size_t i = 0; i < n; ++i) applyH(slots, i, b, L);
}

bool expandGrover(const std::string& recv, const std::vector<ExprPtr>& args,
                  const Location& loc, ExpandCtx& ctx) {
  auto* slots = slotsOf(ctx, recv);
  if (!slots) { emitErr(ctx, "grover: unknown qreg '" + recv + "'", loc); return false; }
  std::int64_t rounds = 1;
  // Args order: oracle, rounds. (Or just rounds when oracle is the
  // kernel parameter referenced by name.)
  for (const auto& a : args) {
    if (auto v = ctx.foldInt(a)) rounds = *v;
  }
  if (rounds < 1) rounds = 1;

  for (std::size_t i = 0; i < slots->size(); ++i)
    applyH(*slots, i, *ctx.builder, ctx.loc);

  for (std::int64_t r = 0; r < rounds; ++r) {
    // oracle call: emit phonon.call "oracle" with no operands (M2
    // contract; the platform driver will inline).
    ctx.builder->call("oracle", {}, {}, ctx.loc);
    diffusion(*slots, *ctx.builder, ctx.loc);
  }
  return true;
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
