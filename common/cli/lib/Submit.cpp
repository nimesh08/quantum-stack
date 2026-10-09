// common/cli/lib/Submit.cpp

#include "qs/common/cli/Submit.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace qs::common::cli {

std::vector<std::string> buildPythonArgv(const SubmitRequest& r) {
  std::string py = std::getenv("QSTACK_PYTHON")
                       ? std::getenv("QSTACK_PYTHON") : "python3";
  std::vector<std::string> a = {
    py, "-m", "spinor_submit", "submit",
    "--qasm-file", r.qasm_path,
    "--chip", r.chip,
    "--provider", r.provider,
    "--shots", std::to_string(r.shots),
    "--mode", toString(r.mode),
    "--program-name", r.program_name,
  };  if (r.api_key_file) {
    a.emplace_back("--api-key-file");
    a.emplace_back(*r.api_key_file);
  }
  if (r.api_key_stdin) {
    a.emplace_back("--api-key-stdin");
  }
  if (r.url) {
    a.emplace_back("--url");
    a.emplace_back(*r.url);
  }
  if (r.region) {
    a.emplace_back("--region");
    a.emplace_back(*r.region);
  }
  if (r.instance) {
    a.emplace_back("--instance");
    a.emplace_back(*r.instance);
  }
  for (const auto& [k, v] : r.extras) {
    a.emplace_back("--extra");
    a.emplace_back(k + "=" + v);
  }
  if (r.verbose) {
    a.emplace_back("--verbose");
  }
  return a;
}

namespace {

#if !defined(_WIN32)
// Read everything from a pipe fd into a string; closes the fd.
std::string slurpFd(int fd) {
  std::string out;
  std::array<char, 4096> buf{};
  while (true) {
    ssize_t n = ::read(fd, buf.data(), buf.size());
    if (n <= 0) break;
    out.append(buf.data(), buf.data() + n);
  }
  ::close(fd);
  return out;
}
#endif

}  // namespace

SubmitResult runPython(const SubmitRequest& r) {
  std::map<std::string, std::string> environment;
  if (const char* extra = std::getenv("QSTACK_PYTHONPATH")) {
    std::string path = extra;
    if (const char* old = std::getenv("PYTHONPATH"))
#ifdef _WIN32
      path += std::string(";") + old;
#else
      path += std::string(":") + old;
#endif
    environment["PYTHONPATH"] = path;
  }
  return runProcess(buildPythonArgv(r), environment);
}
} // namespace qs::common::cli
