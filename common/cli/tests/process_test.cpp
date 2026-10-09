#include "qs/common/cli/Submit.h"
#include "test_main.h"
#include <string>
TEST(Process, argument_boundaries_and_concurrent_capture) {
  std::string argument = "space \"quoted\"; $(echo forbidden) & backslash\\";
  auto r = qs::common::cli::runProcess({PROCESS_HELPER_PATH, argument}, {{"QSTACK_PROCESS_TEST", "child-only"}});
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.exit_code, 7);
  EXPECT_TRUE(r.stdout_text.starts_with(argument + "\nchild-only\n"));
  EXPECT_EQ(r.stderr_text.size(), std::size_t(409600));
  EXPECT_EQ(r.stdout_text.size(), argument.size() + 12 + std::size_t(409600));
}
SPINOR_TEST_MAIN()
