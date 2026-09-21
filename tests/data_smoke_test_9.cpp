// Tests unitaires — data::CssDocument
#define USE_TEST

#include "core/test.hpp"
#include "data/data.hpp"

using namespace data;

TEST(Css, DecodeSimpleRule) {
    CssDocument doc;
    auto err = doc.DecodeStr("body { color: red; margin: 0; }");
    ASSERT_TRUE(err.IsNone());
    auto root = doc.GetRoot();
    ASSERT_TRUE(root != nullptr);
    ASSERT_EQ(root->GetSize(), size_t(1));

    auto rule = root->At(0);
    ASSERT_TRUE(rule != nullptr);
    ASSERT_TRUE(rule->Get("selector") != nullptr);
    EXPECT_EQ(rule->Get("selector")->stringValue, "body");

    auto decls = rule->Get("declarations");
    ASSERT_TRUE(decls != nullptr);
    ASSERT_TRUE(decls->Get("color") != nullptr);
    EXPECT_EQ(decls->Get("color")->stringValue, "red");
    ASSERT_TRUE(decls->Get("margin") != nullptr);
    EXPECT_EQ(decls->Get("margin")->intValue, int64_t(0));
}

TEST(Css, DecodeMediaAtRuleWithNestedBody) {
    CssDocument doc;
    auto err = doc.DecodeStr("@media (max-width: 600px) { .a { color: blue; } }");
    ASSERT_TRUE(err.IsNone());
    auto rule = doc.GetRoot()->At(0);
    ASSERT_TRUE(rule != nullptr);
    ASSERT_TRUE(rule->Get("at") != nullptr);
    EXPECT_EQ(rule->Get("at")->stringValue, "@media");
    ASSERT_TRUE(rule->Get("params") != nullptr);
    EXPECT_EQ(rule->Get("params")->stringValue, "(max-width: 600px)");

    auto body = rule->Get("body");
    ASSERT_TRUE(body != nullptr);
    ASSERT_EQ(body->GetSize(), size_t(1));
    auto nested = body->At(0);
    ASSERT_TRUE(nested != nullptr);
    ASSERT_TRUE(nested->Get("selector") != nullptr);
    EXPECT_EQ(nested->Get("selector")->stringValue, ".a");
}

TEST(Css, DecodeFontFaceDeclarationAtRule) {
    CssDocument doc;
    auto err = doc.DecodeStr("@font-face { font-family: \"MyFont\"; src: url(font.woff); }");
    ASSERT_TRUE(err.IsNone());
    auto rule = doc.GetRoot()->At(0);
    ASSERT_TRUE(rule != nullptr);
    ASSERT_TRUE(rule->Get("at") != nullptr);
    EXPECT_EQ(rule->Get("at")->stringValue, "@font-face");
    EXPECT_TRUE(rule->Has("declarations"));
    EXPECT_FALSE(rule->Has("body"));
}

TEST(Css, DecodeImportInstructionAtRule) {
    CssDocument doc;
    auto err = doc.DecodeStr("@import url(\"theme.css\");");
    ASSERT_TRUE(err.IsNone());
    auto rule = doc.GetRoot()->At(0);
    ASSERT_TRUE(rule != nullptr);
    ASSERT_TRUE(rule->Get("at") != nullptr);
    EXPECT_EQ(rule->Get("at")->stringValue, "@import");
    EXPECT_FALSE(rule->Has("body"));
    EXPECT_FALSE(rule->Has("declarations"));
}

TEST(Css, CommentsAreStripped) {
    CssDocument doc;
    auto err = doc.DecodeStr("/* comment */ body { /* inline */ color: red; }");
    ASSERT_TRUE(err.IsNone());
    auto rule = doc.GetRoot()->At(0);
    ASSERT_TRUE(rule != nullptr);
    ASSERT_TRUE(rule->Get("declarations") != nullptr);
    ASSERT_TRUE(rule->Get("declarations")->Get("color") != nullptr);
    EXPECT_EQ(rule->Get("declarations")->Get("color")->stringValue, "red");
}

TEST(Css, MultipleRulesWithSameSelector) {
    CssDocument doc;
    auto err = doc.DecodeStr(".a { color: red; } .a { margin: 1; }");
    ASSERT_TRUE(err.IsNone());
    // La racine est un Array : deux règles distinctes, pas de fusion.
    ASSERT_EQ(doc.GetRoot()->GetSize(), size_t(2));
}

TEST(Css, UnexpectedClosingBraceIsError) {
    CssDocument doc;
    auto err = doc.DecodeStr("body { color: red; } }");
    EXPECT_TRUE(err.IsSome());
}

TEST(Css, MissingClosingBraceIsError) {
    CssDocument doc;
    auto err = doc.DecodeStr("body { color: red;");
    EXPECT_TRUE(err.IsSome());
}

TEST(Css, EncodeRoundTrip) {
    auto rule = Node::MakeObject();
    rule->Set("selector", Node::MakeString("h1"));
    auto decls = Node::MakeObject();
    decls->Set("font-size", Node::MakeString("2em"));
    rule->Set("declarations", decls);

    auto root = Node::MakeArray();
    root->Push(rule);

    CssDocument doc;
    doc.SetRoot(root);
    String text = doc.EncodeStr();
    EXPECT_TRUE(text.GetSize() > 0);

    CssDocument doc2;
    auto err = doc2.DecodeStr(text);
    ASSERT_TRUE(err.IsNone());
    auto decoded = doc2.GetRoot()->At(0);
    ASSERT_TRUE(decoded != nullptr);
    ASSERT_TRUE(decoded->Get("selector") != nullptr);
    EXPECT_EQ(decoded->Get("selector")->stringValue, "h1");
    ASSERT_TRUE(decoded->Get("declarations") != nullptr);
    ASSERT_TRUE(decoded->Get("declarations")->Get("font-size") != nullptr);
    EXPECT_EQ(decoded->Get("declarations")->Get("font-size")->stringValue, "2em");
}

int main() { return RUN_ALL_TESTS(); }