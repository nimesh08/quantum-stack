// spinor/tests/m7/inspector.cpp
//
// Tiny tool used to (re)generate the M7 goldens. Not run in CI;
// invoked manually by maintainers when the printer convention
// changes deliberately.

#include "spinor/dialect/Spinor.h"
#include "spinor/parser/Parser.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: spinor_m7_inspector FILE.spn\n";
    return 2;
  }
  std::ifstream f(argv[1]);
  if (!f) { std::cerr << "cannot open " << argv[1] << "\n"; return 1; }
  std::ostringstream s; s << f.rdbuf();
  auto r = spinor::parser::parse(s.str(), argv[1]);
  if (!r.module) {
    for (const auto& d : r.diag.items()) {
      std::cerr << "error: " << d.message << " at " << d.loc.file << ":"
                << d.loc.line << ":" << d.loc.column << "\n";
    }
    return 1;
  }
  std::cout << print(*r.module);
  return 0;
}
