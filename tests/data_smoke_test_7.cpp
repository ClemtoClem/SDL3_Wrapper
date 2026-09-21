// Tests unitaires — data::TomlDocument
#define USE_TEST

#include "core/test.hpp"
#include "data/data.hpp"

using namespace data;

TEST(Toml, DecodeTopLevelKeys) {
    TomlDocument doc;
    auto err = doc.DecodeStr("title = \"My App\"\nversion = 2\nratio = 1.5\nenabled = true\n");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot();
    ASSERT_TRUE(root != nullptr);

    ASSERT_TRUE(root->Get("title") != nullptr);
    EXPECT_EQ(root->Get("title")->stringValue, "My App");
    ASSERT_TRUE(root->Get("version") != nullptr);
    EXPECT_EQ(root->Get("version")->intValue, int64_t(2));
    ASSERT_TRUE(root->Get("ratio") != nullptr);
    EXPECT_EQ(root->Get("ratio")->floatValue, 1.5);
    ASSERT_TRUE(root->Get("enabled") != nullptr);
    EXPECT_TRUE(root->Get("enabled")->boolValue);
}

TEST(Toml, DecodeNestedTables) {
    TomlDocument doc;
    auto err = doc.DecodeStr("[server]\nhost = \"localhost\"\n\n[server.db]\nport = 5432\n");
    ASSERT_TRUE(err.IsNone());
    auto server = doc.GetRoot()->Get("server");
    ASSERT_TRUE(server != nullptr);
    ASSERT_TRUE(server->Get("host") != nullptr);
    EXPECT_EQ(server->Get("host")->stringValue, "localhost");

    auto db = server->Get("db");
    ASSERT_TRUE(db != nullptr);
    ASSERT_TRUE(db->Get("port") != nullptr);
    EXPECT_EQ(db->Get("port")->intValue, int64_t(5432));
}

TEST(Toml, DecodeInlineArray) {
    TomlDocument doc;
    auto err = doc.DecodeStr("nums = [1, 2, 3]\n");
    ASSERT_TRUE(err.IsNone());
    auto nums = doc.GetRoot()->Get("nums");
    ASSERT_TRUE(nums != nullptr);
    EXPECT_TRUE(nums->IsArray());
    ASSERT_EQ(nums->GetSize(), size_t(3));
    ASSERT_TRUE(nums->At(2) != nullptr);
    EXPECT_EQ(nums->At(2)->intValue, int64_t(3));
}

TEST(Toml, DecodeNestedInlineArray) {
    TomlDocument doc;
    auto err = doc.DecodeStr("matrix = [[1, 2], [3, 4]]\n");
    ASSERT_TRUE(err.IsNone());
    auto matrix = doc.GetRoot()->Get("matrix");
    ASSERT_TRUE(matrix != nullptr);
    ASSERT_EQ(matrix->GetSize(), size_t(2));
    auto row0 = matrix->At(0);
    ASSERT_TRUE(row0 != nullptr);
    ASSERT_EQ(row0->GetSize(), size_t(2));
    ASSERT_TRUE(row0->At(1) != nullptr);
    EXPECT_EQ(row0->At(1)->intValue, int64_t(2));
}

TEST(Toml, CommentsAreStripped) {
    TomlDocument doc;
    auto err = doc.DecodeStr("# top comment\nkey = 1 # inline comment\n");
    ASSERT_TRUE(err.IsNone());
    ASSERT_TRUE(doc.GetRoot()->Get("key") != nullptr);
    EXPECT_EQ(doc.GetRoot()->Get("key")->intValue, int64_t(1));
}

TEST(Toml, ArrayOfTablesIsUnsupportedError) {
    TomlDocument doc;
    auto err = doc.DecodeStr("[[items]]\nname = \"a\"\n");
    EXPECT_TRUE(err.IsSome());
}

TEST(Toml, MalformedTableHeaderIsError) {
    TomlDocument doc;
    auto err = doc.DecodeStr("[section\nkey = 1\n");
    EXPECT_TRUE(err.IsSome());
}

TEST(Toml, EncodeRoundTrip) {
    auto root = Node::MakeObject();
    root->Set("name", Node::MakeString("test"));
    auto table = Node::MakeObject();
    table->Set("value", Node::MakeInt(5));
    root->Set("section", table);

    TomlDocument doc;
    doc.SetRoot(root);
    String text = doc.EncodeStr();
    EXPECT_TRUE(text.GetSize() > 0);

    TomlDocument doc2;
    auto err = doc2.DecodeStr(text);
    ASSERT_TRUE(err.IsNone());
    auto decoded = doc2.GetRoot();
    ASSERT_TRUE(decoded != nullptr);
    ASSERT_TRUE(decoded->Get("name") != nullptr);
    EXPECT_EQ(decoded->Get("name")->stringValue, "test");
    ASSERT_TRUE(decoded->Get("section") != nullptr);
    ASSERT_TRUE(decoded->Get("section")->Get("value") != nullptr);
    EXPECT_EQ(decoded->Get("section")->Get("value")->intValue, int64_t(5));
}

int main() { return RUN_ALL_TESTS(); }