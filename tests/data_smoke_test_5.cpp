// Tests unitaires — data::YamlDocument
#define USE_TEST

#include "core/test.hpp"
#include "data/data.hpp"

using namespace data;

TEST(Yaml, DecodeSimpleMapping) {
    YamlDocument doc;
    auto err = doc.DecodeStr("name: Alice\nage: 30\nactive: true\n");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot();
    ASSERT_TRUE(root != nullptr);

    ASSERT_TRUE(root->Get("name") != nullptr);
    EXPECT_EQ(root->Get("name")->stringValue, "Alice");
    ASSERT_TRUE(root->Get("age") != nullptr);
    EXPECT_EQ(root->Get("age")->intValue, int64_t(30));
    ASSERT_TRUE(root->Get("active") != nullptr);
    EXPECT_TRUE(root->Get("active")->boolValue);
}

TEST(Yaml, DecodeSequence) {
    YamlDocument doc;
    auto err = doc.DecodeStr("fruits:\n  - apple\n  - banana\n  - cherry\n");
    ASSERT_TRUE(err.IsNone());
    auto fruits = doc.GetRoot()->Get("fruits");
    ASSERT_TRUE(fruits != nullptr);
    EXPECT_TRUE(fruits->IsArray());
    ASSERT_EQ(fruits->GetSize(), size_t(3));
    ASSERT_TRUE(fruits->At(1) != nullptr);
    EXPECT_EQ(fruits->At(1)->stringValue, "banana");
}

TEST(Yaml, DecodeNestedMappingInSequence) {
    YamlDocument doc;
    auto err = doc.DecodeStr("people:\n  - name: Alice\n    age: 30\n  - name: Bob\n    age: 25\n");
    ASSERT_TRUE(err.IsNone());
    auto people = doc.GetRoot()->Get("people");
    ASSERT_TRUE(people != nullptr);
    ASSERT_EQ(people->GetSize(), size_t(2));

    auto alice = people->At(0);
    ASSERT_TRUE(alice != nullptr);
    ASSERT_TRUE(alice->Get("name") != nullptr);
    EXPECT_EQ(alice->Get("name")->stringValue, "Alice");

    auto bob = people->At(1);
    ASSERT_TRUE(bob != nullptr);
    ASSERT_TRUE(bob->Get("age") != nullptr);
    EXPECT_EQ(bob->Get("age")->intValue, int64_t(25));
}

TEST(Yaml, QuotedStringsAndComments) {
    YamlDocument doc;
    auto err = doc.DecodeStr("title: \"Hello: World\"  # a comment\nempty: ~\n");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot();
    ASSERT_TRUE(root != nullptr);

    ASSERT_TRUE(root->Get("title") != nullptr);
    EXPECT_EQ(root->Get("title")->stringValue, "Hello: World");
    ASSERT_TRUE(root->Get("empty") != nullptr);
    EXPECT_TRUE(root->Get("empty")->IsNone());
}

TEST(Yaml, NestedMapping) {
    YamlDocument doc;
    auto err = doc.DecodeStr("server:\n  host: localhost\n  port: 8080\n");
    ASSERT_TRUE(err.IsNone());
    auto server = doc.GetRoot()->Get("server");
    ASSERT_TRUE(server != nullptr);
    ASSERT_TRUE(server->Get("host") != nullptr);
    EXPECT_EQ(server->Get("host")->stringValue, "localhost");
    ASSERT_TRUE(server->Get("port") != nullptr);
    EXPECT_EQ(server->Get("port")->intValue, int64_t(8080));
}

TEST(Yaml, EncodeRoundTrip) {
    auto root = Node::MakeObject();
    root->Set("key", Node::MakeString("value"));
    auto seq = Node::MakeArray();
    seq->Push(Node::MakeInt(1));
    seq->Push(Node::MakeInt(2));
    root->Set("nums", seq);

    YamlDocument doc;
    doc.SetRoot(root);
    String text = doc.EncodeStr();
    EXPECT_TRUE(text.GetSize() > 0);

    YamlDocument doc2;
    auto err = doc2.DecodeStr(text);
    ASSERT_TRUE(err.IsNone());
    auto decoded = doc2.GetRoot();
    ASSERT_TRUE(decoded != nullptr);
    ASSERT_TRUE(decoded->Get("key") != nullptr);
    EXPECT_EQ(decoded->Get("key")->stringValue, "value");

    auto nums = decoded->Get("nums");
    ASSERT_TRUE(nums != nullptr);
    ASSERT_EQ(nums->GetSize(), size_t(2));
    ASSERT_TRUE(nums->At(1) != nullptr);
    EXPECT_EQ(nums->At(1)->intValue, int64_t(2));
}

int main() { return RUN_ALL_TESTS(); }