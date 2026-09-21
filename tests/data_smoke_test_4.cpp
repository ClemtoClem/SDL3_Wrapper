// Tests unitaires — data::XmlDocument
#define USE_TEST

#include "core/test.hpp"
#include "data/data.hpp"

using namespace data;

TEST(Xml, DecodeSimpleElementWithAttributesAndText) {
    XmlDocument doc;
    auto err = doc.DecodeStr(R"(<root id="1"><name>Alice</name></root>)");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot();
    ASSERT_TRUE(root != nullptr);

    auto rootElem = root->Get("root");
    ASSERT_TRUE(rootElem != nullptr);

    auto attrs = rootElem->Get("@attributes");
    ASSERT_TRUE(attrs != nullptr);
    ASSERT_TRUE(attrs->Get("id") != nullptr);
    EXPECT_EQ(attrs->Get("id")->stringValue, "1");

    auto name = rootElem->Get("name");
    ASSERT_TRUE(name != nullptr);
    ASSERT_TRUE(name->Get("#text") != nullptr);
    EXPECT_EQ(name->Get("#text")->stringValue, "Alice");
}

TEST(Xml, RepeatedTagsMergeIntoArray) {
    XmlDocument doc;
    auto err = doc.DecodeStr(R"(<root><item>a</item><item>b</item><item>c</item></root>)");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot()->Get("root");
    ASSERT_TRUE(root != nullptr);

    auto items = root->Get("item");
    ASSERT_TRUE(items != nullptr);
    EXPECT_TRUE(items->IsArray());
    ASSERT_EQ(items->GetSize(), size_t(3));

    ASSERT_TRUE(items->At(0) != nullptr);
    ASSERT_TRUE(items->At(0)->Get("#text") != nullptr);
    EXPECT_EQ(items->At(0)->Get("#text")->stringValue, "a");

    ASSERT_TRUE(items->At(2) != nullptr);
    ASSERT_TRUE(items->At(2)->Get("#text") != nullptr);
    EXPECT_EQ(items->At(2)->Get("#text")->stringValue, "c");
}

TEST(Xml, SelfClosingTagProducesEmptyObject) {
    XmlDocument doc;
    auto err = doc.DecodeStr(R"(<root><empty/></root>)");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot()->Get("root");
    ASSERT_TRUE(root != nullptr);

    auto empty = root->Get("empty");
    ASSERT_TRUE(empty != nullptr);
    EXPECT_TRUE(empty->IsObject());
    EXPECT_EQ(empty->Keys().size(), size_t(0));
}

TEST(Xml, CommentsAreSkipped) {
    XmlDocument doc;
    auto err = doc.DecodeStr(R"(<root><!-- comment --><a>1</a></root>)");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot()->Get("root");
    ASSERT_TRUE(root != nullptr);
    auto a = root->Get("a");
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(a->Get("#text") != nullptr);
    EXPECT_EQ(a->Get("#text")->stringValue, "1");
}

TEST(Xml, LeadingXmlDeclarationIsSkipped) {
    XmlDocument doc;
    auto err = doc.DecodeStr("<?xml version=\"1.0\" encoding=\"UTF-8\"?><root><a>x</a></root>");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot()->Get("root");
    ASSERT_TRUE(root != nullptr);
    ASSERT_TRUE(root->Get("a") != nullptr);
    ASSERT_TRUE(root->Get("a")->Get("#text") != nullptr);
    EXPECT_EQ(root->Get("a")->Get("#text")->stringValue, "x");
}

TEST(Xml, EncodeRoundTrip) {
    auto root = Node::MakeObject();
    auto person = Node::MakeObject();
    auto attrs = Node::MakeObject();
    attrs->Set("id", Node::MakeString("42"));
    person->Set("@attributes", attrs);
    person->Set("#text", Node::MakeString("Bob"));
    root->Set("person", person);

    XmlDocument doc;
    doc.SetRoot(root);
    String text = doc.EncodeStr();
    EXPECT_TRUE(text.GetSize() > 0);

    XmlDocument doc2;
    auto err = doc2.DecodeStr(text);
    ASSERT_TRUE(err.IsNone());
    auto decoded = doc2.GetRoot()->Get("person");
    ASSERT_TRUE(decoded != nullptr);
    ASSERT_TRUE(decoded->Get("@attributes") != nullptr);
    ASSERT_TRUE(decoded->Get("@attributes")->Get("id") != nullptr);
    EXPECT_EQ(decoded->Get("@attributes")->Get("id")->stringValue, "42");
    ASSERT_TRUE(decoded->Get("#text") != nullptr);
    EXPECT_EQ(decoded->Get("#text")->stringValue, "Bob");
}

TEST(Xml, UnmatchedClosingTagIsError) {
    XmlDocument doc;
    auto err = doc.DecodeStr("<root><a></b></root>");
    EXPECT_TRUE(err.IsSome());
}

TEST(Xml, MissingClosingTagIsError) {
    XmlDocument doc;
    auto err = doc.DecodeStr("<root><a>1</a>");
    EXPECT_TRUE(err.IsSome());
}

int main() { return RUN_ALL_TESTS(); }