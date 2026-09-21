// Tests unitaires — data::CsvDocument
#define USE_TEST

#include "core/test.hpp"
#include "data/data.hpp"

using namespace data;

TEST(Csv, DecodeSimpleTable) {
    CsvDocument doc;
    auto err = doc.DecodeStr("name,age\r\nAlice,30\r\nBob,25\r\n");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot();
    ASSERT_TRUE(root != nullptr);
    ASSERT_TRUE(root->Has("name"));
    ASSERT_TRUE(root->Has("age"));

    auto names = root->Get("name");
    ASSERT_TRUE(names != nullptr);
    ASSERT_EQ(names->GetSize(), size_t(2));
    ASSERT_TRUE(names->At(0) != nullptr);
    EXPECT_EQ(names->At(0)->stringValue, "Alice");

    auto ages = root->Get("age");
    ASSERT_TRUE(ages != nullptr);
    ASSERT_TRUE(ages->At(1) != nullptr);
    EXPECT_EQ(ages->At(1)->intValue, int64_t(25));
}

TEST(Csv, QuotedFieldsWithCommaAndNewline) {
    CsvDocument doc;
    auto err = doc.DecodeStr("desc\r\n\"hello, world\"\r\n\"line1\nline2\"\r\n");
    ASSERT_TRUE(err.IsNone());
    auto desc = doc.GetRoot()->Get("desc");
    ASSERT_TRUE(desc != nullptr);
    ASSERT_EQ(desc->GetSize(), size_t(2));

    ASSERT_TRUE(desc->At(0) != nullptr);
    EXPECT_EQ(desc->At(0)->stringValue, "hello, world");

    ASSERT_TRUE(desc->At(1) != nullptr);
    String expected = "line1\nline2";
    EXPECT_EQ(desc->At(1)->stringValue, expected);
}

TEST(Csv, DoubledQuoteEscaping) {
    CsvDocument doc;
    auto err = doc.DecodeStr("word\r\n\"say \"\"hi\"\"\"\r\n");
    ASSERT_TRUE(err.IsNone());
    auto word = doc.GetRoot()->Get("word");
    ASSERT_TRUE(word != nullptr);
    ASSERT_TRUE(word->At(0) != nullptr);
    String expected = "say \"hi\"";
    EXPECT_EQ(word->At(0)->stringValue, expected);
}

TEST(Csv, AutoTypedCells) {
    CsvDocument doc;
    auto err = doc.DecodeStr("n,f,b,s\r\n42,3.5,true,hello\r\n");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot();
    ASSERT_TRUE(root->Get("n")->At(0) != nullptr);
    EXPECT_TRUE(root->Get("n")->At(0)->IsInt());
    EXPECT_TRUE(root->Get("f")->At(0)->IsFloat());
    EXPECT_TRUE(root->Get("b")->At(0)->IsBool());
    EXPECT_TRUE(root->Get("s")->At(0)->IsString());
}

TEST(Csv, UnterminatedQuoteIsError) {
    CsvDocument doc;
    auto err = doc.DecodeStr("a\r\n\"unterminated\r\n");
    EXPECT_TRUE(err.IsSome());
}

TEST(Csv, EncodeRoundTrip) {
    auto root = Node::MakeObject();
    auto colA = Node::MakeArray();
    colA->Push(Node::MakeString("x,y"));
    colA->Push(Node::MakeString("plain"));
    root->Set("col", colA);

    CsvDocument doc;
    doc.SetRoot(root);
    String text = doc.EncodeStr();
    EXPECT_TRUE(text.GetSize() > 0);

    CsvDocument doc2;
    auto err = doc2.DecodeStr(text);
    ASSERT_TRUE(err.IsNone());
    auto col = doc2.GetRoot()->Get("col");
    ASSERT_TRUE(col != nullptr);
    ASSERT_EQ(col->GetSize(), size_t(2));
    ASSERT_TRUE(col->At(0) != nullptr);
    ASSERT_TRUE(col->At(1) != nullptr);
    EXPECT_EQ(col->At(0)->stringValue, "x,y");
    EXPECT_EQ(col->At(1)->stringValue, "plain");
}

int main() { return RUN_ALL_TESTS(); }