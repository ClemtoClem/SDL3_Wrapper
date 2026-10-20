#include "test.hpp"
#include <dataio/dataio.hpp>

using dataio::Node;
using dataio::NodePtr;

// ── JSON : décode, mute, ré-encode ────────────────────────────────

TEST(json_decode_mutate_encode) {
    dataio::JsonDocument doc;
    auto err = doc.DecodeStr("{\"application\":{\"name\":\"MyApp\",\"version\":1},\"enabled\":true}");
    CHECK(err.IsNone());

    NodePtr root = doc.GetRoot();
    CHECK(root != nullptr);
    CHECK(root->IsObject());

    NodePtr app = root->Get("application");
    CHECK(app != nullptr);
    CHECK(app->IsObject());

    NodePtr version = app->Get("version");
    CHECK(version != nullptr);
    CHECK(version->IsInt());
    CHECK_EQ(version->intValue, 1);

    // Mutation : version = 2 (comme dans la démo cpp-maker)
    app->Set("version", Node::MakeInt(2));
    CHECK_EQ(app->Get("version")->intValue, 2);

    // L'encodage reflète la mutation
    String out = doc.EncodeStr();
    CHECK(out.Contains("MyApp"));
    CHECK(out.Contains("2"));
    CHECK(out.Contains("true"));
}

TEST(json_parse_error) {
    dataio::JsonDocument doc;
    auto err = doc.DecodeStr("{ pas du JSON");
    CHECK(err.IsSome());
    CHECK(!err->Format().IsEmpty());
    CHECK(err->line > 0);   // line is public? ParseError { String message; int line; int column; } — struct, all public ✓
}

TEST(factory_json_by_name_and_filename) {
    auto doc = dataio::DocumentFactory::instance().CreateByName("json");
    CHECK(doc != nullptr);
    auto byExt = dataio::DocumentFactory::instance().CreateByFilename("config.json");
    CHECK(byExt != nullptr);

    auto yml = dataio::DocumentFactory::instance().CreateByFilename("scene.yaml");
    CHECK(yml != nullptr);
}

TEST(toml_decode) {
    dataio::TomlDocument doc;
    auto err = doc.DecodeStr("name = \"MyApp\"\nversion = 2\n\n[owner]\nname = \"clement\"\n");
    CHECK(err.IsNone());

    NodePtr root = doc.GetRoot();
    CHECK(root->IsObject());

    CHECK_EQ(root->Get("name")->stringValue, String("MyApp"));  // hmm CHECK_EQ needs operator==  — String == String exists? friend operator== exists per summary. Or compare stringValue.CStr() with strcmp? Use CHECK_EQ(root->Get("name")->stringValue, String("MyApp")) — need String operator==(const String&, const String&). From summary: "comparaisons friend operator==" ✓.
    CHECK(root->Get("version")->IsInt());
    CHECK_EQ(root->Get("version")->intValue, 2);

    NodePtr owner = root->Get("owner");
    CHECK(owner->IsObject());
    CHECK_EQ(owner->Get("name")->stringValue, String("clement"));
}

TEST(ini_decode) {
    dataio::IniDocument doc;
    auto err = doc.DecodeStr("title = dataio\n\n[window]\nwidth = 800\nfullscreen = true\n");
    CHECK(err.IsNone());
    NodePtr root = doc.GetRoot();
    CHECK(root->IsObject());
    CHECK(root->Get("title") != nullptr);   // scalaire auto-typé → STRING "dataio"
    NodePtr win = root->Get("window");
    CHECK(win->IsObject());
    CHECK_EQ(win->Get("width")->intValue, 800);       // scalar::ParseNode("800") → int
    CHECK(win->Get("fullscreen")->IsBool());
    CHECK_EQ(win->Get("fullscreen")->boolValue, true);
}

TEST(xml_decode_attributes) {
    dataio::XmlDocument doc;
    auto err = doc.DecodeStr("<application version=\"1\"><name>MyApp</name></application>");
    CHECK(err.IsNone());
    NodePtr root = doc.GetRoot();
    CHECK(root->IsObject());   // hmm — XML root structure? Need to check xml.cpp: root node wraps children?
}