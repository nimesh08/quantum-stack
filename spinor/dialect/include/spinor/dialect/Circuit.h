#pragma once

#include "spinor/dialect/Spinor.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace spinor::dialect {

inline double numericAttribute(const Op& op, const std::string& key,
                               double fallback = 0.0) {
  for (const auto& a : op.attributes)
    if (a.name == key) {
      if (!std::holds_alternative<double>(a.value) ||
          !std::isfinite(std::get<double>(a.value)))
        throw std::runtime_error("invalid numeric attribute: " + key);
      return std::get<double>(a.value);
    }
  return fallback;
}

struct WireOp {
  OpKind kind;
  std::vector<int> qubits;
  std::vector<Attribute> attributes;
  Location loc;
  int clbit = -1;
};

inline bool isControl(OpKind kind) {
  return kind==OpKind::If||kind==OpKind::Else||kind==OpKind::EndIf;
}
inline bool hasControlFlow(const Module& m) {
  for(const auto& op:m.ops())if(isControl(op.kind))return true;
  return false;
}

inline std::size_t classicalIndex(const Module& m, ValueId bit) {
  const auto id = m.producerOf(bit);
  const auto& op = m.op(id);
  if (op.kind == OpKind::Measure) return std::size_t(numericAttribute(op, "clbit"));
  std::size_t index = 0;
  for (std::uint32_t i = 0; i < id.v; ++i)
    if (m.op(OpId{i}).kind == OpKind::AllocBit) ++index;
  return index;
}

struct WireCircuit {
  std::string name;
  std::string target;
  std::size_t numQubits = 0;
  std::size_t numClbits = 0;
  double globalPhase = 0;
  std::vector<WireOp> instructions;
  std::vector<int> finalLayout;
  std::vector<int> initialLayout;
  std::vector<int> resonatorQubits;
};

inline double parameter(const WireOp& op, const std::string& key = "angle") {
  return numericAttribute(Op{op.kind, {}, {}, op.attributes, op.loc}, key);
}

inline WireCircuit flatten(const Module& m) {
  WireCircuit c;
  c.name = m.name; c.target = m.targetAttr;
  c.numClbits = m.numClbits; c.globalPhase = m.globalPhase;
  c.finalLayout = m.finalLayout;
  c.initialLayout = m.initialLayout;
  c.resonatorQubits = m.resonatorQubits;
  std::vector<int> wire(m.numValues(), -1);
  std::size_t declaredBits = 0, ordinal = 0;
  for (const auto& op : m.ops()) {
    if (op.kind == OpKind::AllocQubit) {
      wire.at(op.results.at(0).v) = static_cast<int>(c.numQubits++);
      continue;
    }
    if (op.kind == OpKind::AllocBit) { ++declaredBits; continue; }
    WireOp inst{op.kind, {}, op.attributes, op.loc};
    for (auto value : op.operands) {
      const int q = wire.at(value.v);
      if (q < 0) throw std::runtime_error("unresolved qubit lineage");
      inst.qubits.push_back(q);
    }
    if (op.kind == OpKind::If) {
      inst.clbit=static_cast<int>(numericAttribute(op,"condition_clbit"));
    }
    if (op.kind == OpKind::Measure) {
      const double index = numericAttribute(op, "clbit", static_cast<double>(ordinal));
      if (index < 0 || index != std::floor(index) || index > 1000000)
        throw std::runtime_error("invalid classical measurement index");
      inst.clbit = static_cast<int>(index);
      c.numClbits = std::max(c.numClbits, std::size_t(inst.clbit + 1));
      ++ordinal;
    } else {
      for (std::size_t k = 0; k < op.results.size(); ++k)
        wire.at(op.results[k].v) = inst.qubits.at(k);
    }
    c.instructions.push_back(std::move(inst));
  }
  c.numClbits = std::max(c.numClbits, declaredBits);
  return c;
}

inline Module rebuild(const WireCircuit& c) {
  Module m;
  m.name = c.name; m.targetAttr = c.target;
  m.globalPhase = c.globalPhase; m.numClbits = c.numClbits;
  m.finalLayout = c.finalLayout;
  m.initialLayout = c.initialLayout;
  m.resonatorQubits = c.resonatorQubits;
  Builder b(m);
  std::vector<ValueId> live;
  for (std::size_t i = 0; i < c.numQubits; ++i) {
    auto q = b.allocQubit(); m.setName(q, "q" + std::to_string(i));
    live.push_back(q);
  }
  for (const auto& inst : c.instructions) {
    Op op{inst.kind, {}, {}, inst.attributes, inst.loc};
    for (int q : inst.qubits) op.operands.push_back(live.at(q));
    auto id = m.addOp(std::move(op));
    if (inst.kind == OpKind::Measure) {
      auto bit = m.addValue(bitType(), id);
      m.opMut(id).results.push_back(bit);
      setMeasurementTarget(m, bit, inst.clbit);
      m.setName(bit, "c" + std::to_string(inst.clbit) + "_" + std::to_string(id.v));
    } else if (inst.kind != OpKind::Barrier) {
      for (int q : inst.qubits) {
        auto value = m.addValue(qubitType(), id);
        m.opMut(id).results.push_back(value);
        live.at(q) = value;
      }
    }
  }
  return m;
}

} // namespace spinor::dialect
