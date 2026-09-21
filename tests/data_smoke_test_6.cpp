// Tests unitaires — data::IniDocument
#define USE_TEST

#include "core/test.hpp"
#include "data/data.hpp"

using namespace data;

TEST(Ini, DecodeGlobalAndSections) {
    IniDocument doc;
    auto err = doc.DecodeStr("debug = true\n\n[server]\nhost = localhost\nport = 8080\n");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot();
    ASSERT_TRUE(root != nullptr);

    ASSERT_TRUE(root->Get("debug") != nullptr);
    EXPECT_TRUE(root->Get("debug")->boolValue);

    auto server = root->Get("server");
    ASSERT_TRUE(server != nullptr);
    EXPECT_TRUE(server->IsObject());
    ASSERT_TRUE(server->Get("host") != nullptr);
    EXPECT_EQ(server->Get("host")->stringValue, "localhost");
    ASSERT_TRUE(server->Get("port") != nullptr);
    EXPECT_EQ(server->Get("port")->intValue, int64_t(8080));
}

TEST(Ini, CommentsAreIgnored) {
    IniDocument doc;
    auto err = doc.DecodeStr("; a comment\n# another comment\nkey = 1\n");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot();
    ASSERT_TRUE(root != nullptr);
    ASSERT_TRUE(root->Get("key") != nullptr);
    EXPECT_EQ(root->Get("key")->intValue, int64_t(1));
}

TEST(Ini, MultipleSections) {
    IniDocument doc;
    auto err = doc.DecodeStr("[a]\nx = 1\n[b]\nx = 2\n");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot();
    ASSERT_TRUE(root->Get("a") != nullptr);
    ASSERT_TRUE(root->Get("b") != nullptr);
    ASSERT_TRUE(root->Get("a")->Get("x") != nullptr);
    ASSERT_TRUE(root->Get("b")->Get("x") != nullptr);
    EXPECT_EQ(root->Get("a")->Get("x")->intValue, int64_t(1));
    EXPECT_EQ(root->Get("b")->Get("x")->intValue, int64_t(2));
}

TEST(Ini, MissingEqualsIsError) {
    IniDocument doc;
    auto err = doc.DecodeStr("not_a_valid_line_without_equals\n");
    EXPECT_TRUE(err.IsSome());
}

TEST(Ini, MissingClosingBracketIsError) {
    IniDocument doc;
    auto err = doc.DecodeStr("[section\nkey = 1\n");
    EXPECT_TRUE(err.IsSome());
}

TEST(Ini, EncodeRoundTrip) {
    auto root = Node::MakeObject();
    root->Set("debug", Node::MakeBool(false));
    auto section = Node::MakeObject();
    section->Set("port", Node::MakeInt(9090));
    root->Set("server", section);

    IniDocument doc;
    doc.SetRoot(root);
    String text = doc.EncodeStr();
    EXPECT_TRUE(text.GetSize() > 0);

    IniDocument doc2;
    auto err = doc2.DecodeStr(text);
    ASSERT_TRUE(err.IsNone());
    auto decoded = doc2.GetRoot();
    ASSERT_TRUE(decoded != nullptr);
    ASSERT_TRUE(decoded->Get("debug") != nullptr);
    EXPECT_FALSE(decoded->Get("debug")->boolValue);
    ASSERT_TRUE(decoded->Get("server") != nullptr);
    ASSERT_TRUE(decoded->Get("server")->Get("port") != nullptr);
    EXPECT_EQ(decoded->Get("server")->Get("port")->intValue, int64_t(9090));
}

int main() { return RUN_ALL_TESTS(); }