// spinor/tests/m4/yaml_test.cpp
//
// M4 yaml subset parser tests.

#include "../../registry/lib/Yaml.h"
#include "test_main.h"

using namespace spinor::registry::yaml;

TEST(M4_yaml, parse_scalars) {
  Node n = Parser::parse("answer: 42\npi: 3.14\nname: \"abc\"\n");
  EXPECT_TRUE(n.isMap());
  EXPECT_EQ(n.at("answer").asInt(), static_cast<long long>(42));
  EXPECT_EQ(n.at("pi").asDouble(), 3.14);
  EXPECT_STREQ(n.at("name").asString(), "abc");
}

TEST(M4_yaml, parse_block_mapping) {
  Node n = Parser::parse(
      "outer:\n  inner: 1\n  name: hello\nother: 2\n");
  EXPECT_TRUE(n.at("outer").isMap());
  EXPECT_EQ(n.at("outer").at("inner").asInt(), 1LL);
  EXPECT_STREQ(n.at("outer").at("name").asString(), "hello");
  EXPECT_EQ(n.at("other").asInt(), 2LL);
}

TEST(M4_yaml, parse_block_sequence) {
  Node n = Parser::parse(
      "items:\n  - one\n  - two\n  - three\n");
  EXPECT_TRUE(n.at("items").isArray());
  EXPECT_EQ(n.at("items").asArray().size(), static_cast<size_t>(3));
  EXPECT_STREQ(n.at("items").asArray()[1].asString(), "two");
}

TEST(M4_yaml, parse_flow_sequence) {
  Node n = Parser::parse("gates: [h, x, cx]\n");
  EXPECT_TRUE(n.at("gates").isArray());
  EXPECT_EQ(n.at("gates").asArray().size(), static_cast<size_t>(3));
  EXPECT_STREQ(n.at("gates").asArray()[2].asString(), "cx");
}

TEST(M4_yaml, parse_nested_flow) {
  Node n = Parser::parse("edges: [[0,1],[1,2]]\n");
  EXPECT_TRUE(n.at("edges").isArray());
  EXPECT_EQ(n.at("edges").asArray()[0].asArray()[0].asInt(), 0LL);
  EXPECT_EQ(n.at("edges").asArray()[1].asArray()[1].asInt(), 2LL);
}

TEST(M4_yaml, parse_comments_skipped) {
  Node n = Parser::parse("# leading\nkey: 1 # trailing\n");
  EXPECT_EQ(n.at("key").asInt(), 1LL);
}

TEST(M4_yaml, parse_bool_null) {
  Node n = Parser::parse("a: true\nb: false\nc: null\nd: ~\n");
  EXPECT_TRUE(n.at("a").asBool());
  EXPECT_FALSE(n.at("b").asBool());
  EXPECT_TRUE(n.at("c").isNull());
  EXPECT_TRUE(n.at("d").isNull());
}

TEST(M4_yaml, parse_multiline_string) {
  Node n = Parser::parse("notes: |\n  abc\n  def\nother: 1\n");
  EXPECT_TRUE(n.at("notes").isString());
  EXPECT_STREQ(n.at("notes").asString(), "abc\ndef");
  EXPECT_EQ(n.at("other").asInt(), 1LL);
}

SPINOR_TEST_MAIN()
