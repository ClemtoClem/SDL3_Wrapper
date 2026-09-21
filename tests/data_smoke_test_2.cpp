// Tests unitaires — data::Reader, data::scalar::, data::DocumentFactory
#define USE_TEST

#include "core/test.hpp"
#include "data/data.hpp"

using namespace data;

// ── Reader ───────────────────────────────────────────────────────────────

TEST(Reader, PeekGetEofBasic) {
    Reader r(StringView("ab"));
    EXPECT_FALSE(r.Eof());

    auto p = r.Peek();
    ASSERT_TRUE(p.IsSome());
    EXPECT_EQ(*p, 'a');

    auto g1 = r.Get();
    ASSERT_TRUE(g1.IsSome());
    EXPECT_EQ(*g1, 'a');

    auto g2 = r.Get();
    ASSERT_TRUE(g2.IsSome());
    EXPECT_EQ(*g2, 'b');

    EXPECT_TRUE(r.Eof());
    EXPECT_TRUE(r.Peek().IsNone());
    EXPECT_TRUE(r.Get().IsNone());
}

TEST(Reader, PutbackAndEat) {
    Reader r(StringView("xy"));
    r.Get(); // consomme 'x'
    r.Putback();

    auto p = r.Peek();
    ASSERT_TRUE(p.IsSome());
    EXPECT_EQ(*p, 'x');

    EXPECT_TRUE(r.Eat('x'));
    EXPECT_FALSE(r.Eat('z')); // ne consomme rien si ça ne correspond pas
    EXPECT_TRUE(r.Eat('y'));
    EXPECT_TRUE(r.Eof());
}

TEST(Reader, SkipWsAndLineTracking) {
    Reader r(StringView("  \n  a"));
    EXPECT_EQ(r.Line(), 1);
    r.SkipWs();

    auto p = r.Peek();
    ASSERT_TRUE(p.IsSome());
    EXPECT_EQ(*p, 'a');
    EXPECT_EQ(r.Line(), 2);
}

TEST(Reader, RemainingAndPos) {
    Reader r(StringView("hello"));
    r.Get();
    r.Get();
    EXPECT_EQ(r.Pos(), size_t(2));
    EXPECT_EQ(String(r.Remaining()), "llo");
}

// ── scalar:: ─────────────────────────────────────────────────────────────

TEST(Scalar, ParseBoolVariants) {
    auto t = scalar::ParseBool("true");
    ASSERT_TRUE(t.IsSome());
    EXPECT_TRUE(*t);

    auto f = scalar::ParseBool("FALSE");
    ASSERT_TRUE(f.IsSome());
    EXPECT_FALSE(*f);

    EXPECT_TRUE(scalar::ParseBool("yes").IsNone());
    EXPECT_TRUE(scalar::ParseBool("").IsNone());
}

TEST(Scalar, ParseIntAndFloat) {
    auto i = scalar::ParseInt("42");
    ASSERT_TRUE(i.IsSome());
    EXPECT_EQ(*i, int64_t(42));
    EXPECT_TRUE(scalar::ParseInt("abc").IsNone());
    EXPECT_TRUE(scalar::ParseInt("3.5").IsNone()); // pas un entier

    auto f = scalar::ParseFloat("3.5");
    ASSERT_TRUE(f.IsSome());
    EXPECT_EQ(*f, 3.5);
    EXPECT_TRUE(scalar::ParseFloat("abc").IsNone());
}

TEST(Scalar, ParseNodePicksMostPreciseType) {
    auto b = scalar::ParseNode("true");
    ASSERT_TRUE(b != nullptr);
    EXPECT_TRUE(b->IsBool());

    auto i = scalar::ParseNode("42");
    ASSERT_TRUE(i != nullptr);
    EXPECT_TRUE(i->IsInt());

    auto f = scalar::ParseNode("3.14");
    ASSERT_TRUE(f != nullptr);
    EXPECT_TRUE(f->IsFloat());

    auto s = scalar::ParseNode("hello world");
    ASSERT_TRUE(s != nullptr);
    EXPECT_TRUE(s->IsString());
}

TEST(Scalar, ToStringRoundTrip) {
    EXPECT_EQ(scalar::ToString(Node::MakeBool(true)), "true");
    EXPECT_EQ(scalar::ToString(Node::MakeBool(false)), "false");
    EXPECT_EQ(scalar::ToString(Node::MakeInt(7)), "7");
    EXPECT_EQ(scalar::ToString(Node::MakeString("abc")), "abc");
    EXPECT_EQ(scalar::ToString(nullptr), "");
}

// ── DocumentFactory ──────────────────────────────────────────────────────

TEST(DocumentFactoryTest, CreateByFilenameKnownExtensions) {
    auto &factory = DocumentFactory::instance();

    EXPECT_TRUE(factory.CreateByFilename("config.json") != nullptr);
    EXPECT_TRUE(factory.CreateByFilename("config.xml") != nullptr);
    EXPECT_TRUE(factory.CreateByFilename("config.yaml") != nullptr);
    EXPECT_TRUE(factory.CreateByFilename("config.yml") != nullptr);
    EXPECT_TRUE(factory.CreateByFilename("config.ini") != nullptr);
    EXPECT_TRUE(factory.CreateByFilename("config.cfg") != nullptr);
    EXPECT_TRUE(factory.CreateByFilename("config.toml") != nullptr);
    EXPECT_TRUE(factory.CreateByFilename("config.csv") != nullptr);
    EXPECT_TRUE(factory.CreateByFilename("style.css") != nullptr);
}

TEST(DocumentFactoryTest, CreateByFilenameIsCaseInsensitiveOnExtension) {
    auto &factory = DocumentFactory::instance();
    EXPECT_TRUE(factory.CreateByFilename("CONFIG.JSON") != nullptr);
}

TEST(DocumentFactoryTest, CreateByNameKnownFormats) {
    auto &factory = DocumentFactory::instance();
    EXPECT_TRUE(factory.CreateByName("json") != nullptr);
    EXPECT_TRUE(factory.CreateByName("yaml") != nullptr);
    EXPECT_TRUE(factory.CreateByName("css") != nullptr);
}

TEST(DocumentFactoryTest, CreateByUnknownReturnsNull) {
    auto &factory = DocumentFactory::instance();
    EXPECT_TRUE(factory.CreateByFilename("config.unknownext") == nullptr);
    EXPECT_TRUE(factory.CreateByFilename("noextension") == nullptr);
    EXPECT_TRUE(factory.CreateByName("does-not-exist") == nullptr);
}

TEST(DocumentFactoryTest, CreateByFilenameProducesWorkingDecoder) {
    auto doc = DocumentFactory::instance().CreateByFilename("data.json");
    ASSERT_TRUE(doc != nullptr);
    auto err = doc->DecodeStr(R"({"ok": true})");
    ASSERT_TRUE(err.IsNone());
    ASSERT_TRUE(doc->GetRoot() != nullptr);
    ASSERT_TRUE(doc->GetRoot()->Get("ok") != nullptr);
    EXPECT_TRUE(doc->GetRoot()->Get("ok")->boolValue);
}

int main() { return RUN_ALL_TESTS(); }