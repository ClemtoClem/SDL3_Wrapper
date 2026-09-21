// Tests unitaires — data::JsonDocument
#define USE_TEST

#include "core/test.hpp"
#include "data/data.hpp"

using namespace data;

TEST(Json, DecodeSimpleObject) {
    JsonDocument doc;
    auto err = doc.DecodeStr(R"({"name": "Alice", "age": 30, "active": true, "score": 3.5, "note": null})");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot();
    ASSERT_TRUE(root != nullptr);
    ASSERT_TRUE(root->IsObject());

    ASSERT_TRUE(root->Get("name") != nullptr);
    EXPECT_EQ(root->Get("name")->stringValue, "Alice");
    ASSERT_TRUE(root->Get("age") != nullptr);
    EXPECT_EQ(root->Get("age")->intValue, int64_t(30));
    ASSERT_TRUE(root->Get("active") != nullptr);
    EXPECT_TRUE(root->Get("active")->boolValue);
    ASSERT_TRUE(root->Get("score") != nullptr);
    EXPECT_EQ(root->Get("score")->floatValue, 3.5);
    ASSERT_TRUE(root->Get("note") != nullptr);
    EXPECT_TRUE(root->Get("note")->IsNone());
}

TEST(Json, DecodeNestedArrayAndObject) {
    JsonDocument doc;
    auto err = doc.DecodeStr(R"({"items": [1, 2, 3], "nested": {"x": 1}})");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot();
    ASSERT_TRUE(root != nullptr);

    auto items = root->Get("items");
    ASSERT_TRUE(items != nullptr);
    EXPECT_TRUE(items->IsArray());
    ASSERT_EQ(items->GetSize(), size_t(3));
    ASSERT_TRUE(items->At(1) != nullptr);
    EXPECT_EQ(items->At(1)->intValue, int64_t(2));

    auto nested = root->Get("nested");
    ASSERT_TRUE(nested != nullptr);
    ASSERT_TRUE(nested->Get("x") != nullptr);
    EXPECT_EQ(nested->Get("x")->intValue, int64_t(1));
}

TEST(Json, DecodeStringEscapes) {
    JsonDocument doc;
    auto err = doc.DecodeStr(R"({"s": "line1\nline2\t\"quoted\""})");
    ASSERT_TRUE(err.IsNone());
    auto s = doc.GetRoot()->Get("s");
    ASSERT_TRUE(s != nullptr);
    String expected = "line1\nline2\t\"quoted\"";
    EXPECT_EQ(s->stringValue, expected);
}

TEST(Json, DecodeEmptyObjectAndArray) {
    JsonDocument doc;
    auto err = doc.DecodeStr("{}");
    ASSERT_TRUE(err.IsNone());
    ASSERT_TRUE(doc.GetRoot() != nullptr);
    EXPECT_EQ(doc.GetRoot()->GetSize(), size_t(0));

    JsonDocument doc2;
    auto err2 = doc2.DecodeStr(R"({"empty": []})");
    ASSERT_TRUE(err2.IsNone());
    auto empty = doc2.GetRoot()->Get("empty");
    ASSERT_TRUE(empty != nullptr);
    EXPECT_TRUE(empty->IsArray());
    EXPECT_EQ(empty->GetSize(), size_t(0));
}

TEST(Json, EncodeRoundTrip) {
    auto obj = Node::MakeObject();
    obj->Set("a", Node::MakeInt(1));
    obj->Set("b", Node::MakeString("hi"));
    auto arr = Node::MakeArray();
    arr->Push(Node::MakeBool(true));
    arr->Push(Node::MakeBool(false));
    obj->Set("c", arr);

    JsonDocument doc;
    doc.SetRoot(obj);
    String text = doc.EncodeStr();
    EXPECT_TRUE(text.GetSize() > 0);

    JsonDocument doc2;
    auto err = doc2.DecodeStr(text);
    ASSERT_TRUE(err.IsNone());
    auto root = doc2.GetRoot();
    ASSERT_TRUE(root != nullptr);

    ASSERT_TRUE(root->Get("a") != nullptr);
    EXPECT_EQ(root->Get("a")->intValue, int64_t(1));
    ASSERT_TRUE(root->Get("b") != nullptr);
    EXPECT_EQ(root->Get("b")->stringValue, "hi");

    auto c = root->Get("c");
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->GetSize(), size_t(2));
    ASSERT_TRUE(c->At(0) != nullptr);
    ASSERT_TRUE(c->At(1) != nullptr);
    EXPECT_TRUE(c->At(0)->boolValue);
    EXPECT_FALSE(c->At(1)->boolValue);
}

TEST(Json, DecodeMissingValueIsError) {
    JsonDocument doc;
    // Valeur manquante après "b": (accolade fermante directement)
    auto err = doc.DecodeStr("{\n  \"a\": 1,\n  \"b\": \n}");
    ASSERT_TRUE(err.IsSome());
    EXPECT_TRUE(err->line >= 1);
}

TEST(Json, DecodeMissingCommaIsError) {
    JsonDocument doc;
    auto err = doc.DecodeStr(R"({"a": 1 "b": 2})");
    EXPECT_TRUE(err.IsSome());
}

TEST(Json, DecodeUnterminatedStringIsError) {
    JsonDocument doc;
    auto err = doc.DecodeStr(R"({"a": "unterminated)");
    EXPECT_TRUE(err.IsSome());
}

int main() { return RUN_ALL_TESTS(); }