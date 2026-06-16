// spinor/submit/lib/LocalProvider.cpp
//
// Runs the M8 simulator and samples shots from the resulting
// state vector.

#include "spinor/submit/Provider.h"
#include "spinor/sim/Simulator.h"

#include <complex>
#include <random>
#include <sstream>

namespace spinor::submit {

namespace {
std::string bitstring(std::size_t value, std::size_t nQubits) {
  std::string s(nQubits, '0');
  for (std::size_t i = 0; i < nQubits; ++i) {
    if (value & (std::size_t(1) << i)) s[nQubits - 1 - i] = '1';
  }
  return s;
}
}  // namespace

Job LocalProvider::submit(const dialect::Module& m,
                          const registry::ChipInfo& /*chip*/,
                          std::size_t shots) {
  Job j;
  j.id = "local-" + std::to_string(nextId_++);
  j.provider = "local";
  j.status = "completed";
  Histogram hist;
  hist.shots = shots;
  sim::StateVector sv = sim::simulate(m);
  // Sample shots from |amp|^2 distribution.
  std::vector<double> probs(sv.amps.size());
  double total = 0.0;
  for (std::size_t i = 0; i < sv.amps.size(); ++i) {
    probs[i] = std::norm(sv.amps[i]);
    total += probs[i];
  }
  if (total <= 0.0) {
    // Empty/all-zero state — fall back to uniform.
    for (auto& p : probs) p = 1.0 / static_cast<double>(probs.size());
    total = 1.0;
  }
  for (auto& p : probs) p /= total;
  std::uniform_real_distribution<double> u(0.0, 1.0);
  for (std::size_t s = 0; s < shots; ++s) {
    double r = u(rng_);
    double acc = 0.0;
    std::size_t pick = probs.size() - 1;
    for (std::size_t i = 0; i < probs.size(); ++i) {
      acc += probs[i];
      if (r <= acc) { pick = i; break; }
    }
    ++hist.counts[bitstring(pick, sv.qubits)];
  }
  jobs_[j.id] = {j.status, std::move(hist)};
  return j;
}

Job LocalProvider::poll(const std::string& jobId) {
  Job j;
  j.id = jobId;
  j.provider = "local";
  auto it = jobs_.find(jobId);
  j.status = (it == jobs_.end()) ? "error" : it->second.status;
  return j;
}

Histogram LocalProvider::results(const std::string& jobId) {
  auto it = jobs_.find(jobId);
  if (it == jobs_.end()) return {};
  return it->second.hist;
}

}  // namespace spinor::submit
