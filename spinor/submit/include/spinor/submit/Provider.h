// spinor/submit/include/spinor/submit/Provider.h
//
// Uniform submission interface. Phase A ships a `LocalProvider`
// that runs the M8 statevector simulator; the IBM/Braket/Azure
// adapters live in Python under `spinor/submit/python/` (decision
// D8) and shell out via the same conceptual contract.

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/registry/Registry.h"

#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace spinor::submit {

struct Job {
  std::string id;     // unique, opaque
  std::string status; // "queued" | "running" | "completed" | "error"
  std::string provider;
};

struct Histogram {
  // bitstring (e.g. "00", "11") -> number of shots that produced it
  std::map<std::string, std::size_t> counts;
  std::size_t shots = 0;
};

class IProvider {
 public:
  virtual ~IProvider() = default;
  virtual std::string name() const = 0;
  // Submit returns a job handle.
  virtual Job submit(const dialect::Module& m,
                     const registry::ChipInfo& chip,
                     std::size_t shots) = 0;
  virtual Job poll(const std::string& jobId) = 0;
  virtual Histogram results(const std::string& jobId) = 0;
};

// LocalProvider: runs the M8 statevector simulator and returns a
// sampled histogram. Used in end-to-end tests without network.
class LocalProvider : public IProvider {
 public:
  explicit LocalProvider(std::uint64_t seed = 42) : rng_(seed) {}
  std::string name() const override { return "local"; }
  Job submit(const dialect::Module& m, const registry::ChipInfo& chip,
             std::size_t shots) override;
  Job poll(const std::string& jobId) override;
  Histogram results(const std::string& jobId) override;

 private:
  std::mt19937_64 rng_;
  struct Stored {
    std::string status;
    Histogram hist;
  };
  std::map<std::string, Stored> jobs_;
  std::uint64_t nextId_ = 1;
};

}  // namespace spinor::submit
