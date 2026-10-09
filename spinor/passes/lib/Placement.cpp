// spinor/passes/lib/Placement.cpp

#include "spinor/passes/Placement.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <queue>
#include <stdexcept>

namespace spinor::passes {

namespace {

using namespace spinor::dialect;

// Build a per-value→virtual-index map by walking alloc_qubit ops.
struct VirtualIndex {
  std::vector<int> ofValue;  // size = m.numValues()
  int total = 0;
};

VirtualIndex buildVirtualIndex(const Module& m) {
  VirtualIndex vi;
  vi.ofValue.assign(m.numValues(), -1);
  // Track allocs first, then propagate through ops.
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit) {
      ValueId v = op.results.front();
      vi.ofValue[v.v] = vi.total++;
      continue;
    }
    int nq = qubitArity(op.kind);
    if (nq <= 0) continue;
    int qResults = nq;
    if (op.kind == OpKind::Measure) qResults = 0;
    for (int k = 0; k < qResults && k < (int)op.results.size(); ++k) {
      ValueId in = op.operands[k];
      ValueId out = op.results[k];
      if (in.v < vi.ofValue.size() && vi.ofValue[in.v] >= 0 &&
          out.v < vi.ofValue.size()) {
        vi.ofValue[out.v] = vi.ofValue[in.v];
      }
    }
  }
  return vi;
}

}  // namespace

Layout Placement::run(const dialect::Module& m,
                      const CouplingGraph& g) const {
  VirtualIndex vi = buildVirtualIndex(m);
  int V = vi.total;
  std::size_t P = g.qubits();
  if (static_cast<std::size_t>(V) > P)
    throw std::runtime_error("placement: circuit exceeds target qubit capacity");

  Layout layout;
  layout.v2p.assign(V, -1);
  layout.p2v.assign(P, -1);

  if (V == 0) return layout;

  // Interaction graph.
  std::map<std::pair<int, int>, int> weight;
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (qubitArity(op.kind) != 2) continue;
    if (op.kind == OpKind::Barrier) continue;
    int a = vi.ofValue[op.operands[0].v];
    int b = vi.ofValue[op.operands[1].v];
    if (a < 0 || b < 0 || a == b) continue;
    int lo = std::min(a, b), hi = std::max(a, b);
    weight[{lo, hi}] += 1;
  }

  // Sort interaction pairs by weight, descending.
  std::vector<std::pair<std::pair<int, int>, int>> pairs(weight.begin(),
                                                          weight.end());
  std::sort(pairs.begin(), pairs.end(),
            [](const auto& x, const auto& y) {
              if (x.second != y.second) return x.second > y.second;
              return x.first < y.first;
            });

  // Reserve a connected physical component for each interacting logical
  // component. Disabled/isolated physical wires must not attract a qubit
  // merely because they have a low index.
  std::vector<int> physicalComponent(P,-1),logicalComponent(V,-1);
  std::vector<std::vector<int>> physicalGroups,logicalGroups,logicalNeighbors(V);
  for(const auto& [edge,w]:weight){logicalNeighbors[edge.first].push_back(edge.second);logicalNeighbors[edge.second].push_back(edge.first);}
  for(std::size_t start=0;start<P;++start)if(physicalComponent[start]<0){
    int id=int(physicalGroups.size());physicalGroups.emplace_back();std::queue<int> todo;todo.push(int(start));physicalComponent[start]=id;
    while(!todo.empty()){int q=todo.front();todo.pop();physicalGroups.back().push_back(q);for(int n:g.neighbours(q))if(physicalComponent[n]<0){physicalComponent[n]=id;todo.push(n);}}
  }
  for(int start=0;start<V;++start)if(logicalComponent[start]<0){
    int id=int(logicalGroups.size());logicalGroups.emplace_back();std::queue<int> todo;todo.push(start);logicalComponent[start]=id;
    while(!todo.empty()){int q=todo.front();todo.pop();logicalGroups.back().push_back(q);for(int n:logicalNeighbors[q])if(logicalComponent[n]<0){logicalComponent[n]=id;todo.push(n);}}
  }
  std::vector<int> groupOrder;for(std::size_t i=0;i<logicalGroups.size();++i)groupOrder.push_back(int(i));
  std::stable_sort(groupOrder.begin(),groupOrder.end(),[&](int a,int b){return logicalGroups[a].size()>logicalGroups[b].size();});
  std::vector<std::size_t> remaining;for(const auto& group:physicalGroups)remaining.push_back(group.size());
  std::vector<int> reserved(logicalGroups.size(),-1);
  for(int group:groupOrder){
    int selected=-1;for(std::size_t p=0;p<physicalGroups.size();++p)if(remaining[p]>=logicalGroups[group].size()&&(selected<0||remaining[p]<remaining[selected]))selected=int(p);
    if(selected<0)throw std::runtime_error("placement: no physical connected component can hold an interacting logical component");
    reserved[group]=selected;remaining[selected]-=logicalGroups[group].size();
  }
  auto eligible=[&](int v,int p){return physicalComponent[p]==reserved[logicalComponent[v]];};

  auto unassigned = [&](int v) { return v >= 0 && v < V && layout.v2p[v] < 0; };
  auto place = [&](int v, int p) {
    layout.v2p[v] = p;
    layout.p2v[p] = v;
  };

  // For each top-weight pair, place both onto a connected
  // physical pair.
  for (const auto& [pp, w] : pairs) {
    int u = pp.first, v = pp.second;
    if (!unassigned(u) && !unassigned(v)) continue;
    if (unassigned(u) && unassigned(v)) {
      // pick the lowest-index connected physical pair where both
      // are free.
      bool placed = false;
      for (std::size_t a = 0; a < P && !placed; ++a) {
        if (layout.p2v[a] >= 0||!eligible(u,int(a))) continue;
        for (int b : g.neighbours(static_cast<int>(a))) {
          if (b <= (int)a) continue;
          if (layout.p2v[b] >= 0||!eligible(v,b)) continue;
          place(u, static_cast<int>(a));
          place(v, b);
          placed = true;
          break;
        }
      }
      if (!placed) {
        // graph too sparse; fall back to any free pair.
        for (std::size_t a = 0; a < P && !placed; ++a) {
          if (layout.p2v[a] >= 0||!eligible(u,int(a))) continue;
          for (std::size_t b = a + 1; b < P; ++b) {
            if (layout.p2v[b] >= 0||!eligible(v,int(b))) continue;
            place(u, (int)a);
            place(v, (int)b);
            placed = true;
            break;
          }
        }
      }
    } else {
      int known = unassigned(u) ? v : u;
      int needs = unassigned(u) ? u : v;
      int phys = layout.v2p[known];
      // place `needs` on a free neighbour of `phys`, else any free.
      bool placed = false;
      for (int n : g.neighbours(phys)) {
        if (layout.p2v[n] < 0) {
          place(needs, n);
          placed = true;
          break;
        }
      }
      if (!placed) {
        std::vector<bool> visited(P,false);std::queue<int> todo;todo.push(phys);visited[phys]=true;
        while(!todo.empty()&&!placed){
          int candidate=todo.front();todo.pop();
          if(layout.p2v[candidate]<0&&eligible(needs,candidate)){place(needs,candidate);placed=true;break;}
          for(int n:g.neighbours(candidate))if(!visited[n]){visited[n]=true;todo.push(n);}
        }
        if(!placed)throw std::runtime_error("placement: no connected free physical qubit");
      }
    }
  }

  // Any still-unassigned virtual qubits go to the lowest-index
  // free physical qubits.
  for (int u = 0; u < V; ++u) {
    if (!unassigned(u)) continue;
    for (std::size_t a = 0; a < P; ++a) {
      if (layout.p2v[a] < 0&&eligible(u,int(a))) {
        place(u, (int)a);
        break;
      }
    }
  }

  return layout;
}

}  // namespace spinor::passes
