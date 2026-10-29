#include "test.hpp"
#include <core/core.hpp>

#include <set>

TEST(Uuid_GeneratedNamedAndParsed) {
	std::set<std::pair<uint64_t, uint64_t>> seen;
	for (int i = 0; i < 1000; ++i) {
		const Uuid id = Uuid::Generate();
		CHECK(!id.IsNil());
		CHECK_EQ(id.Version(), 4);
		seen.insert({id.high, id.low});
	}
	CHECK_EQ(seen.size(), size_t(1000));
	const Uuid space = Uuid::Generate();
	CHECK(Uuid::FromName(space, StringView("a/b.gltf")) == Uuid::FromName(space, StringView("a/b.gltf")));
	CHECK(Uuid::FromName(space, StringView("a/b.gltf")) != Uuid::FromName(space, StringView("a/c.gltf")));
	CHECK_EQ(Uuid::FromName(space, StringView("x")).Version(), 5);
	const Uuid id = Uuid::Generate();
	const String text = id.ToString();
	CHECK_EQ(text.GetSize(), size_t(36));
	CHECK(Uuid::Parse(text.View()).Unwrap() == id);
	CHECK(Uuid::Parse(StringView("pas-un-uuid")).IsNone());
}

