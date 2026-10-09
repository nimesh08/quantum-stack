#include <cstdlib>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  if(argc != 2) return 9;
  std::cout << argv[1] << '\n';
  if(const char* value = std::getenv("QSTACK_PROCESS_TEST")) std::cout << value << '\n';
  // Exceed both pipe buffers. A launcher that drains sequentially will deadlock.
  for(int i = 0; i < 100; ++i) {
    std::cerr << std::string(4096, 'e') << std::flush;
    std::cout << std::string(4096, 'o') << std::flush;
  }
  return 7;
}
