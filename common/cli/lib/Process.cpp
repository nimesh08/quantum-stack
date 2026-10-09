#include "qs/common/cli/Submit.h"
#include <array>
#include <cstdlib>
#include <future>
#include <sstream>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace qs::common::cli {
namespace {
#ifdef _WIN32
std::wstring wide(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  if (n) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}
std::wstring quote(const std::wstring& arg) {
  std::wstring out = L"\"";
  size_t slashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') { ++slashes; continue; }
    out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0; out += c;
  }
  out.append(slashes * 2, L'\\');
  return out + L'"';
}
std::string drain(HANDLE handle) {
  std::string result; std::array<char, 8192> buffer{}; DWORD n;
  while (ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &n, nullptr) && n)
    result.append(buffer.data(), n);
  CloseHandle(handle); return result;
}
#else
std::string drain(int fd) {
  std::string result; std::array<char,8192> buffer{};
  for (;;) {
    auto n = ::read(fd, buffer.data(), buffer.size());
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    result.append(buffer.data(), static_cast<size_t>(n));
  }
  ::close(fd); return result;
}
#endif
std::string jsonString(const std::string& s) {
  std::string out = "\"";
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
    else out += c;
  }
  return out + '"';
}
}

SubmitResult runProcess(const std::vector<std::string>& argv,
                        const std::map<std::string, std::string>& envOverrides) {
  SubmitResult result; result.exit_code = 127;
  if (argv.empty()) { result.stderr_text = "empty process command"; return result; }
#ifdef _WIN32
  SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE outRead=nullptr, outWrite=nullptr, errRead=nullptr, errWrite=nullptr;
  if (!CreatePipe(&outRead, &outWrite, &sa, 0) || !CreatePipe(&errRead, &errWrite, &sa, 0)) {
    if(outRead) CloseHandle(outRead); if(outWrite) CloseHandle(outWrite);
    result.stderr_text = "Cannot create subprocess pipes"; return result;
  }
  SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESTDHANDLES;
  startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE); startup.hStdOutput=outWrite; startup.hStdError=errWrite;
  PROCESS_INFORMATION process{};
  std::wstring command;
  for(const auto& arg:argv) { if(!command.empty()) command += L' '; command += quote(wide(arg)); }
  struct CaseLess { bool operator()(const std::wstring& a,const std::wstring& b) const {return _wcsicmp(a.c_str(),b.c_str())<0;} };
  std::map<std::wstring,std::wstring,CaseLess> env;
  auto block=GetEnvironmentStringsW();
  for (auto p=block; p && *p; p+=wcslen(p)+1) {
    std::wstring entry(p); auto eq=entry.find(L'=',1);
    if(eq!=std::wstring::npos) env[entry.substr(0,eq)]=entry.substr(eq+1);
  }
  if(block) FreeEnvironmentStringsW(block);
  for(const auto& [k,v]:envOverrides) env[wide(k)]=wide(v);
  std::vector<wchar_t> environment;
  for(const auto& [k,v]:env) { auto entry=k+L"="+v; environment.insert(environment.end(),entry.begin(),entry.end());environment.push_back(0); }
  environment.push_back(0);
  BOOL started=CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,
                             environment.data(),nullptr,&startup,&process);
  CloseHandle(outWrite); CloseHandle(errWrite);
  if(!started) { CloseHandle(outRead);CloseHandle(errRead);result.stderr_text="Cannot start subprocess (Windows error "+std::to_string(GetLastError())+")";return result; }
  auto stdoutFuture=std::async(std::launch::async,[&]{return drain(outRead);});
  auto stderrFuture=std::async(std::launch::async,[&]{return drain(errRead);});
  WaitForSingleObject(process.hProcess,INFINITE); DWORD code=127; GetExitCodeProcess(process.hProcess,&code);
  CloseHandle(process.hProcess);CloseHandle(process.hThread);
  result.exit_code=static_cast<int>(code);
#else
  int outPipe[2],errPipe[2];
  if(pipe(outPipe)!=0) {result.stderr_text="Cannot create subprocess stdout pipe";return result;}
  if(pipe(errPipe)!=0) {close(outPipe[0]);close(outPipe[1]);result.stderr_text="Cannot create subprocess stderr pipe";return result;}
  auto pid=fork();
  if(pid==0) {
    for(const auto& [k,v]:envOverrides) setenv(k.c_str(),v.c_str(),1);
    dup2(outPipe[1],STDOUT_FILENO);dup2(errPipe[1],STDERR_FILENO);
    close(outPipe[0]);close(outPipe[1]);close(errPipe[0]);close(errPipe[1]);
    std::vector<char*> args;for(const auto& arg:argv) args.push_back(const_cast<char*>(arg.c_str()));args.push_back(nullptr);
    execvp(args[0],args.data()); _exit(127);
  }
  close(outPipe[1]);close(errPipe[1]);
  if(pid<0) {close(outPipe[0]);close(errPipe[0]);result.stderr_text="Cannot fork subprocess";return result;}
  auto stdoutFuture=std::async(std::launch::async,[&]{return drain(outPipe[0]);});
  auto stderrFuture=std::async(std::launch::async,[&]{return drain(errPipe[0]);});
  int status=0;while(waitpid(pid,&status,0)<0 && errno==EINTR) {}
  result.exit_code=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
#endif
  result.stdout_text=stdoutFuture.get();result.stderr_text=stderrFuture.get();
  result.ok=result.exit_code==0;return result;
}

SubmitResult runQstack(const std::vector<std::string>& args) {
  std::string py=std::getenv("QSTACK_PYTHON")?std::getenv("QSTACK_PYTHON"):
#ifdef _WIN32
    "python";
#else
    "python3";
#endif
  std::map<std::string,std::string> environment;
  std::string forwarded="[";
  for(size_t i=0;i<args.size();++i) {if(i)forwarded+=',';forwarded+=jsonString(args[i]);}
  environment["QSTACK_FORWARDED_ARGS"]=forwarded+"]";
  if(const char* extra=std::getenv("QSTACK_PYTHONPATH")) {
    std::string path=extra;
    if(const char* old=std::getenv("PYTHONPATH")) path+=
#ifdef _WIN32
      std::string(";")+old;
#else
      std::string(":")+old;
#endif
    environment["PYTHONPATH"]=path;
  }
  return runProcess({py,"-m","qstack"},environment);
}
}
