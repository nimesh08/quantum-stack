// spinor/tests/support/test_main.h
//
// Minimal test harness for the Spinor C++ test suite. Self-contained
// (no external dependency) so M1 tests run on the bare image.
//
// Use:
//
//   #include "test_main.h"
//   TEST(M1_builder, value_uniqueness) {
//     EXPECT_EQ(2 + 2, 4);
//   }
//   SPINOR_TEST_MAIN()  // exactly one per executable
//
// `#pragma once` is intentionally avoided so the SPINOR_TEST_MAIN macro
// can be defined at the call site.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace spinor::test {

struct TestCase {
  std::string suite;
  std::string name;
  std::function<void()> fn;
  std::string file;
  int line;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

struct Registrar {
  Registrar(std::string suite, std::string name,
            std::function<void()> fn, std::string file, int line) {
    registry().push_back(
        {std::move(suite), std::move(name), std::move(fn),
         std::move(file), line});
  }
};

struct AssertionFailure : std::exception {
  std::string msg;
  const char* what() const noexcept override { return msg.c_str(); }
};

inline void fail(const std::string& m) {
  AssertionFailure f;
  f.msg = m;
  throw f;
}

inline std::string filterArg(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--filter=", 0) == 0) return a.substr(9);
  }
  return {};
}

inline int run(int argc, char** argv) {
  std::string filter = filterArg(argc, argv);
  std::size_t pass = 0;
  std::size_t fail_n = 0;
  std::vector<std::string> failures;
  for (auto& tc : registry()) {
    std::string full = tc.suite + "." + tc.name;
    if (!filter.empty() && full.find(filter) == std::string::npos) {
      continue;
    }
    std::cout << "[ RUN      ] " << full << "\n";
    try {
      tc.fn();
      std::cout << "[       OK ] " << full << "\n";
      ++pass;
    } catch (const AssertionFailure& f) {
      std::cout << "[  FAILED  ] " << full << ": " << f.what() << "\n";
      failures.push_back(full + ": " + f.what());
      ++fail_n;
    } catch (const std::exception& e) {
      std::cout << "[  FAILED  ] " << full << ": uncaught " << e.what()
                << "\n";
      failures.push_back(full + ": uncaught " + e.what());
      ++fail_n;
    } catch (...) {
      std::cout << "[  FAILED  ] " << full
                << ": uncaught non-std exception\n";
      failures.push_back(full + ": uncaught non-std exception");
      ++fail_n;
    }
  }
  std::cout << "\n[==========] " << pass << " passed, " << fail_n
            << " failed.\n";
  if (!failures.empty()) {
    std::cout << "\nFailures:\n";
    for (const auto& f : failures) std::cout << "  " << f << "\n";
  }
  return fail_n == 0 ? 0 : 1;
}

}  // namespace spinor::test

#define SPINOR_CONCAT2(a, b) a##b
#define SPINOR_CONCAT(a, b) SPINOR_CONCAT2(a, b)

#define TEST(suite_, name_)                                                  \
  static void SPINOR_CONCAT(test_fn_, __LINE__)();                           \
  static ::spinor::test::Registrar SPINOR_CONCAT(test_reg_, __LINE__)(       \
      #suite_, #name_, &SPINOR_CONCAT(test_fn_, __LINE__),                   \
      __FILE__, __LINE__);                                                   \
  static void SPINOR_CONCAT(test_fn_, __LINE__)()

#define SPINOR_FAIL_IF_NOT(cond, expr)                                       \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::ostringstream _os;                                                \
      _os << __FILE__ << ":" << __LINE__ << " — assertion failed: "          \
          << expr;                                                           \
      ::spinor::test::fail(_os.str());                                       \
    }                                                                        \
  } while (false)

#define EXPECT_TRUE(x)                                                       \
  SPINOR_FAIL_IF_NOT(static_cast<bool>(x), "EXPECT_TRUE(" #x ")")
#define EXPECT_FALSE(x)                                                      \
  SPINOR_FAIL_IF_NOT(!static_cast<bool>(x), "EXPECT_FALSE(" #x ")")
#define EXPECT_EQ(a, b)                                                      \
  do {                                                                       \
    auto _a = (a);                                                           \
    auto _b = (b);                                                           \
    if (!(_a == _b)) {                                                       \
      std::ostringstream _os;                                                \
      _os << __FILE__ << ":" << __LINE__ << " — EXPECT_EQ(" #a ", " #b       \
          << ") failed: lhs=" << _a << " rhs=" << _b;                        \
      ::spinor::test::fail(_os.str());                                       \
    }                                                                        \
  } while (false)
#define EXPECT_NE(a, b)                                                      \
  do {                                                                       \
    auto _a = (a);                                                           \
    auto _b = (b);                                                           \
    if (_a == _b) {                                                          \
      std::ostringstream _os;                                                \
      _os << __FILE__ << ":" << __LINE__ << " — EXPECT_NE(" #a ", " #b       \
          << ") failed: both=" << _a;                                        \
      ::spinor::test::fail(_os.str());                                       \
    }                                                                        \
  } while (false)
#define EXPECT_STREQ(a, b)                                                   \
  do {                                                                       \
    std::string _a = (a);                                                    \
    std::string _b = (b);                                                    \
    if (_a != _b) {                                                          \
      std::ostringstream _os;                                                \
      _os << __FILE__ << ":" << __LINE__                                     \
          << " — EXPECT_STREQ failed.\n--- expected ---\n"                   \
          << _b << "\n--- got ---\n" << _a;                                  \
      ::spinor::test::fail(_os.str());                                       \
    }                                                                        \
  } while (false)
#define EXPECT_CONTAINS(haystack, needle)                                    \
  do {                                                                       \
    std::string _h = (haystack);                                             \
    std::string _n = (needle);                                               \
    if (_h.find(_n) == std::string::npos) {                                  \
      std::ostringstream _os;                                                \
      _os << __FILE__ << ":" << __LINE__                                     \
          << " — EXPECT_CONTAINS failed.\nneedle: " << _n                    \
          << "\nhaystack: " << _h;                                           \
      ::spinor::test::fail(_os.str());                                       \
    }                                                                        \
  } while (false)

#define SPINOR_TEST_MAIN()                                                   \
  int main(int argc, char** argv) {                                          \
    return ::spinor::test::run(argc, argv);                                  \
  }
