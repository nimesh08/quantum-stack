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
  SubmitResult res;
  auto argv = buildPythonArgv(r);

  // Honour QSTACK_PYTHONPATH so installs that aren't pip-editable can
  // still locate spinor_submit by exporting PYTHONPATH=<repo>/spinor/
  // submit/python before calling the binary.
  const char* extraPath = std::getenv("QSTACK_PYTHONPATH");
  std::string ppEnv;
  if (extraPath && *extraPath) {
    const char* old = std::getenv("PYTHONPATH");
    ppEnv = std::string("PYTHONPATH=") + extraPath +
            (old && *old ? std::string(":") + old : std::string());
  }

#if defined(_WIN32)
  // Windows path: build a single command line, use _popen for stdout
  // capture. stderr is left attached to the parent for now (good
  // enough for v1; an MSI / installer-published build can rework
  // this later).
  std::ostringstream cmd;
  for (size_t i = 0; i < argv.size(); ++i) {
    if (i) cmd << ' ';
    cmd << '"' << argv[i] << '"';
  }
  FILE* pipe = _popen(cmd.str().c_str(), "r");
  if (!pipe) {
    res.stderr_text = "could not start python: " + std::string(std::strerror(errno));
    return res;
  }
  std::array<char, 4096> buf{};
  while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
    res.stdout_text.append(buf.data());
  }
  int rc = _pclose(pipe);
  res.exit_code = rc;
  res.ok = (rc == 0);
  return res;
#else
  // POSIX path: pipe stdout + stderr separately; fork + execvp.
  int outp[2], errp[2];
  if (::pipe(outp) != 0 || ::pipe(errp) != 0) {
    res.stderr_text = "pipe() failed: " + std::string(std::strerror(errno));
    return res;
  }
  pid_t pid = ::fork();
  if (pid < 0) {
    res.stderr_text = "fork() failed: " + std::string(std::strerror(errno));
    return res;
  }
  if (pid == 0) {
    // Child.
    if (!ppEnv.empty()) ::putenv(const_cast<char*>(ppEnv.c_str()));
    ::dup2(outp[1], 1);
    ::dup2(errp[1], 2);
    ::close(outp[0]); ::close(outp[1]);
    ::close(errp[0]); ::close(errp[1]);
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
    cargv.push_back(nullptr);
    ::execvp(cargv[0], cargv.data());
    // exec failed.
    std::string m = "execvp: " + std::string(std::strerror(errno)) + "\n";
    ::write(2, m.data(), m.size());
    _exit(127);
  }
  // Parent.
  ::close(outp[1]);
  ::close(errp[1]);
  res.stdout_text = slurpFd(outp[0]);
  res.stderr_text = slurpFd(errp[0]);
  int status = 0;
  ::waitpid(pid, &status, 0);
  res.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  res.ok = (res.exit_code == 0);
  return res;
#endif
}

}  // namespace qs::common::cli
