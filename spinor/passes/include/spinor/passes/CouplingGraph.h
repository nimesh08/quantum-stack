// spinor/passes/include/spinor/passes/CouplingGraph.h

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace spinor::passes {

class CouplingGraph {
 public:
  CouplingGraph(std::size_t n, std::vector<std::pair<int, int>> edges,
                bool allToAll);

  std::size_t qubits() const { return n_; }
  bool allToAll() const { return allToAll_; }

  // True if a-b edge exists (or always when all-to-all and a!=b).
  bool connected(int a, int b) const;
  // Shortest-path length, INT_MAX if disconnected.
  int distance(int a, int b) const;

  // Sorted ascending neighbour list.
  const std::vector<int>& neighbours(int u) const { return adj_[u]; }
  const std::vector<std::pair<int, int>>& edges() const { return edgeList_; }

 private:
  std::size_t n_ = 0;
  bool allToAll_ = false;
  std::vector<std::pair<int, int>> edgeList_;
  std::vector<std::vector<int>> adj_;
  std::vector<std::vector<int>> dist_;  // n×n
};

}  // namespace spinor::passes
