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
  hist.counts = sim::sample(m, shots, rng_);
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
