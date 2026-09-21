// Tests unitaires — data::Node (arbre commun)
#define USE_TEST

#include "core/test.hpp"
#include "data/node.hpp"

using namespace data;

TEST(Node, MakeScalarsAndPredicates) {
    auto none = Node::MakeNone();
    EXPECT_TRUE(none->IsNone());
    EXPECT_FALSE(none->IsScalar());

    auto s = Node::MakeString("hello");
    EXPECT_TRUE(s->IsString());
    EXPECT_TRUE(s->IsScalar());
    EXPECT_EQ(s->stringValue, "hello");

    auto b = Node::MakeBool(true);
    EXPECT_TRUE(b->IsBool());
    EXPECT_TRUE(b->boolValue);

    auto i = Node::MakeInt(42);
    EXPECT_TRUE(i->IsInt());
    EXPECT_TRUE(i->IsNumber());
    EXPECT_EQ(i->intValue, int64_t(42));

    auto f = Node::MakeFloat(3.5);
    EXPECT_TRUE(f->IsFloat());
    EXPECT_TRUE(f->IsNumber());
    EXPECT_EQ(f->floatValue, 3.5);

    auto obj = Node::MakeObject();
    EXPECT_TRUE(obj->IsObject());
    EXPECT_FALSE(obj->IsScalar());

    auto arr = Node::MakeArray();
    EXPECT_TRUE(arr->IsArray());
    EXPECT_FALSE(arr->IsScalar());
}

TEST(Node, ObjectSetGetHasRemove) {
    auto obj = Node::MakeObject();
    EXPECT_FALSE(obj->Has("a"));
    EXPECT_TRUE(obj->Get("a") == nullptr);

    obj->Set("a", Node::MakeInt(1));
    obj->Set("b", Node::MakeString("x"));

    ASSERT_TRUE(obj->Has("a"));
    ASSERT_TRUE(obj->Has("b"));
    ASSERT_TRUE(obj->Get("a") != nullptr);
    ASSERT_TRUE(obj->Get("b") != nullptr);
    EXPECT_EQ(obj->Get("a")->intValue, int64_t(1));
    EXPECT_EQ(obj->Get("b")->stringValue, "x");

    ASSERT_EQ(obj->Keys().size(), size_t(2));
    EXPECT_EQ(obj->Keys()[0], "a");
    EXPECT_EQ(obj->Keys()[1], "b");

    // Écraser une clé existante ne doit ni la dupliquer dans l'ordre ni la
    // déplacer.
    obj->Set("a", Node::MakeInt(2));
    ASSERT_EQ(obj->Keys().size(), size_t(2));
    EXPECT_EQ(obj->Keys()[0], "a");
    ASSERT_TRUE(obj->Get("a") != nullptr);
    EXPECT_EQ(obj->Get("a")->intValue, int64_t(2));

    obj->Remove("a");
    EXPECT_FALSE(obj->Has("a"));
    EXPECT_TRUE(obj->Get("a") == nullptr);
    ASSERT_EQ(obj->Keys().size(), size_t(1));
    EXPECT_EQ(obj->Keys()[0], "b");
}

TEST(Node, ArrayPushAt) {
    auto arr = Node::MakeArray();
    EXPECT_EQ(arr->GetSize(), size_t(0));

    arr->Push(Node::MakeInt(10));
    arr->Push(Node::MakeInt(20));
    arr->Push(Node::MakeInt(30));

    ASSERT_EQ(arr->GetSize(), size_t(3));
    ASSERT_TRUE(arr->At(0) != nullptr);
    ASSERT_TRUE(arr->At(1) != nullptr);
    ASSERT_TRUE(arr->At(2) != nullptr);
    EXPECT_EQ(arr->At(0)->intValue, int64_t(10));
    EXPECT_EQ(arr->At(1)->intValue, int64_t(20));
    EXPECT_EQ(arr->At(2)->intValue, int64_t(30));

    // Hors limites : At() retourne nullptr plutôt que de planter.
    EXPECT_TRUE(arr->At(3) == nullptr);
}

TEST(Node, CloneIsDeepCopy) {
    auto root = Node::MakeObject();
    auto list = Node::MakeArray();
    list->Push(Node::MakeInt(1));
    list->Push(Node::MakeInt(2));
    root->Set("list", list);
    root->Set("name", Node::MakeString("orig"));

    auto copy = root->Clone();
    ASSERT_TRUE(copy != nullptr);

    auto copyName = copy->Get("name");
    auto copyList = copy->Get("list");
    ASSERT_TRUE(copyName != nullptr);
    ASSERT_TRUE(copyList != nullptr);

    // Modifier la copie ne doit pas affecter l'original (copie profonde).
    copyName->stringValue = "changed";
    copyList->Push(Node::MakeInt(3));

    ASSERT_TRUE(root->Get("name") != nullptr);
    ASSERT_TRUE(root->Get("list") != nullptr);
    EXPECT_EQ(root->Get("name")->stringValue, "orig");
    EXPECT_EQ(root->Get("list")->GetSize(), size_t(2));
    EXPECT_EQ(copy->Get("list")->GetSize(), size_t(3));
}

int main() { return RUN_ALL_TESTS(); }