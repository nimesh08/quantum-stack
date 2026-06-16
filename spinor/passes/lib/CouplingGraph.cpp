// spinor/passes/lib/CouplingGraph.cpp

#include "spinor/passes/CouplingGraph.h"

#include <algorithm>
#include <climits>
#include <queue>

namespace spinor::passes {

CouplingGraph::CouplingGraph(std::size_t n,
                             std::vector<std::pair<int, int>> edges,
                             bool allToAll)
    : n_(n), allToAll_(allToAll), edgeList_(std::move(edges)) {
  adj_.assign(n_, {});
  if (allToAll_) {
    for (std::size_t i = 0; i < n_; ++i) {
      adj_[i].reserve(n_ - 1);
      for (std::size_t j = 0; j < n_; ++j) {
        if (i != j) adj_[i].push_back(static_cast<int>(j));
      }
    }
  } else {
    for (auto [a, b] : edgeList_) {
      if (a >= 0 && b >= 0 &&
          static_cast<std::size_t>(a) < n_ &&
          static_cast<std::size_t>(b) < n_) {
        adj_[a].push_back(b);
        adj_[b].push_back(a);
      }
    }
    for (auto& v : adj_) {
      std::sort(v.begin(), v.end());
      v.erase(std::unique(v.begin(), v.end()), v.end());
    }
  }
  // BFS-per-node for distance matrix.
  dist_.assign(n_, std::vector<int>(n_, INT_MAX));
  for (std::size_t s = 0; s < n_; ++s) {
    dist_[s][s] = 0;
    std::queue<int> q;
    q.push(static_cast<int>(s));
    while (!q.empty()) {
      int u = q.front(); q.pop();
      for (int v : adj_[u]) {
        if (dist_[s][v] == INT_MAX) {
          dist_[s][v] = dist_[s][u] + 1;
          q.push(v);
        }
      }
    }
  }
}

bool CouplingGraph::connected(int a, int b) const {
  if (a == b) return false;
  if (a < 0 || b < 0 || static_cast<std::size_t>(a) >= n_ ||
      static_cast<std::size_t>(b) >= n_) {
    return false;
  }
  if (allToAll_) return true;
  const auto& nb = adj_[a];
  return std::binary_search(nb.begin(), nb.end(), b);
}

int CouplingGraph::distance(int a, int b) const {
  if (a < 0 || b < 0 || static_cast<std::size_t>(a) >= n_ ||
      static_cast<std::size_t>(b) >= n_) {
    return INT_MAX;
  }
  return dist_[a][b];
}

}  // namespace spinor::passes
