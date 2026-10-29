#include "test.hpp"
#include <core/core.hpp>

#include <sstream>
#include <unordered_map>

using namespace string_literals;

// ===========================================================================
// SUITE : StringTest
// ===========================================================================

TEST(Construction) {
	CHECK(String().IsEmpty());
	CHECK_EQ(String("hello"), "hello");
	CHECK_EQ(String(std::string("hi")), "hi");
	CHECK_EQ(String(StringView("sv")), "sv");
	CHECK_EQ(String('A'), "A");
	CHECK_EQ(String(3, 'x'), "xxx");
	CHECK_EQ("world"_s, "world");
	CHECK_EQ(String(String("abc")), "abc");

	String s1;
	s1 = "xyz";
	CHECK_EQ(s1, "xyz");

	String s2;
	s2 = StringView("sv2");
	CHECK_EQ(s2, "sv2");
}

TEST(Factories) {
	CHECK_EQ(String::From(42), "42");
	CHECK_EQ(String::From(10, 2), "1010");
	CHECK_EQ(String::From(255, 16), "ff");
	CHECK_EQ(String::From(-7), "-7");
	CHECK_EQ(String::From(3.14f, 2), "3.14");
	CHECK_EQ(String::From(2.71828, 3), "2.718");
	CHECK_EQ(String::From(true), "true");
	CHECK_EQ(String::From(false), "false");
	CHECK_EQ(String::Format("%d+%d=%d", 1, 2, 3), "1+2=3");
	CHECK_EQ(String::Format("%.1f", 3.14), "3.1");
}

TEST(Access) {
	String s("hello");
	CHECK_EQ(s.GetSize(), 5);
	CHECK_EQ(s.Length(), 5);
	CHECK(!s.IsEmpty());
	CHECK_EQ(s[0], 'h');
	CHECK_EQ(s.CharAt(4), 'o');
	CHECK_EQ(s.Front(), 'h');
	CHECK_EQ(s.Back(), 'o');
	CHECK_EQ(std::string(s.CStr()), "hello");
	CHECK_EQ(s.View(), "hello");

	String e;
	CHECK(e.IsEmpty());
	CHECK(!e);
	CHECK(!!s);
}

TEST(Comparison) {
	String a("abc"), b("abc"), c("xyz");
	CHECK(a == b);
	CHECK(a == "abc");
	CHECK(a != c);
	CHECK(a < c);
	CHECK(c > a);
	CHECK(a <= b);
	CHECK(a >= b);
	CHECK_EQ(a.Compare(b), 0);
	CHECK(a.Compare(c) < 0);
	CHECK(a.Equals("abc"));
	CHECK(String("Hello").EqualsIgnoreCase(String("hELLO")));
	CHECK(!String("abc").EqualsIgnoreCase(String("abcd")));
}

TEST(Search) {
	String s("hello world hello");
	CHECK_EQ(s.Find('o'), 4);
	CHECK_EQ(s.Find("world"), 6);
	CHECK_EQ(s.Find('o', 5), 7);
	CHECK_EQ(s.Rfind('o'), 16);
	CHECK_EQ(s.Rfind("hello"), 12);
	CHECK_EQ(s.IndexOf('o'), 4);
	CHECK_EQ(s.IndexOf(String("world")), 6);
	CHECK_EQ(s.LastIndexOf('o'), 16);
	CHECK(s.Contains("world"));
	CHECK(!s.Contains("xyz"));
	CHECK(s.StartsWith("hello"));
	CHECK(s.EndsWith("hello"));
	CHECK(s.StartsWith(String("world"), 6));
	CHECK(s.EndsWith(String("hello")));
	CHECK_EQ(s.Count('l'), 5);
	CHECK_EQ(s.Count("hello"), 2);
	CHECK_EQ(s.Find("zzz"), String::NPOS);
}

TEST(Extraction) {
	String s("hello world");
	CHECK_EQ(s.Substr(6), "world");
	CHECK_EQ(s.Substr(0, 5), "hello");
	CHECK_EQ(s.Substring(6, 11), "world");
	CHECK_EQ(s.Substring(6), "world");

	auto between = String("<tag>content</tag>").GetBetween("<tag>", "</tag>");
	CHECK(between.IsSome());
	CHECK_EQ(*between, "content");

	auto none = String("no tags").GetBetween("<", ">");
	CHECK(!none.IsSome());
}

TEST(Concat) {
	String s("foo");
	s += "bar";
	CHECK_EQ(s, "foobar");
	s += 42;
	CHECK_EQ(s, "foobar42");
	s += 3.14;
	CHECK(s.StartsWith("foobar42"));

	String a = "a"_s + "b"_s + "c"_s;
	CHECK_EQ(a, "abc");
	CHECK_EQ(String("x") + "yz", "xyz");
	CHECK(std::string("pre") + "fix" == "prefix");

	String c;
	c.Concat("hello");
	c.Concat(' ');
	c.Concat(42);
	CHECK(c.StartsWith("hello "));

	String b("x");
	b.Append("y").Append("z").Append(size_t(1), '!');
	CHECK_EQ(b, "xyz!");
}

TEST(Transform) {
	CHECK_EQ(String("HÉLLO").ToLower(), "hÉllo");
	CHECK_EQ(String("hello").ToUpper(), "HELLO");
	CHECK_EQ(String("HELLO").UToLower(), "hello");
	CHECK_EQ(String("hello").UToUpper(), "HELLO");

	String t("  hello  ");
	CHECK_EQ(t.Trim(), "hello");
	CHECK_EQ(t.TrimLeft(), "hello  ");
	CHECK_EQ(t.TrimRight(), "  hello");

	CHECK_EQ(String("aabbcc").Replace("bb", "XX"), "aaXXcc");
	CHECK_EQ(String("aaa").Replace('a', 'b'), "bbb");
	CHECK_EQ(String("abc").Replace("x", "y"), "abc");

	CHECK_EQ(String("hello").Remove(1, 3), "ho");
	CHECK_EQ(String("helo").Insert(3, String("l")), "hello");
	CHECK_EQ(String("hllo").Insert(1, 'e'), "hello");

	CHECK_EQ(String("ab").Repeat(3), "ababab");
	CHECK_EQ(String("ab").Repeat(0), "");
	CHECK_EQ(String("abc").ReverseBytes(), "cba");

	CHECK_EQ(String("42").PadLeft(5), "   42");
	CHECK_EQ(String("42").PadRight(5), "42   ");
	CHECK_EQ(String("hi").PadCenter(6), "  hi  ");
	CHECK_EQ(String("7").PadLeft(3, '0'), "007");
	CHECK_EQ(String("hello").Truncate(3), "hel");
	CHECK_EQ(String("hi").Truncate(10), "hi");
}

TEST(SplitJoin) {
	auto parts = String("a,b,c").Split(",");
	CHECK_EQ(parts.size(), 3);
	CHECK_EQ(parts[0], "a");
	CHECK_EQ(parts[2], "c");

	auto limited = String("a:b:c:d").Split(":", 2);
	CHECK_EQ(limited.size(), 3);
	CHECK_EQ(limited[2], "c:d");

	auto any = String("a,b;c").SplitAny(",;");
	CHECK_EQ(any.size(), 3);

	auto ls = String("foo\nbar\r\nbaz").Lines();
	CHECK_EQ(ls.size(), 3);
	CHECK_EQ(ls[0], "foo");
	CHECK_EQ(ls[2], "baz");

	auto ch = String("hi").Chars();
	CHECK_EQ(ch.size(), 2);
	CHECK_EQ(ch[0], "h");

	CHECK_EQ(String::Join({"a", "b", "c"}, "-"), "a-b-c");
	CHECK_EQ(String::Join({"x", "y"}), "xy");
	CHECK_EQ(String::Join(String("1,2,3").Split(","), "|"), "1|2|3");
}

TEST(Functional) {
	String r =
		String("hello").Map([](CodepointT cp) -> CodepointT { return (cp >= 'a' && cp <= 'z') ? cp - 32 : cp; });
	CHECK_EQ(r, "HELLO");

	String digits = String("a1b2c3").Filter([](CodepointT cp) { return cp >= '0' && cp <= '9'; });
	CHECK_EQ(digits, "123");

	String doubled = String("abc").FlatMap([](CodepointT cp) -> String {
		std::string s;
		s += static_cast<char>(cp);
		s += static_cast<char>(cp);
		return String(s);
	});
	CHECK_EQ(doubled, "aabbcc");

	size_t total = String("hello").Reduce(size_t(0), [](size_t acc, CodepointT) { return acc + 1; });
	CHECK_EQ(total, 5);

	CHECK(String("abc3").Any([](CodepointT cp) { return cp >= '0' && cp <= '9'; }));
	CHECK(!String("abc").Any([](CodepointT cp) { return cp >= '0' && cp <= '9'; }));
	CHECK(String("abc").All([](CodepointT cp) { return cp >= 'a' && cp <= 'z'; }));
	CHECK(!String("aBc").All([](CodepointT cp) { return cp >= 'a' && cp <= 'z'; }));

	auto shout = [](String s) { return s.ToUpper().Append("!"); };
	auto trim = [](String s) { return s.Trim(); };
	String piped = String("  hello  ") | trim | shout;
	CHECK_EQ(piped, "HELLO!");
}

TEST(Predicates) {
	CHECK(String("").IsEmpty());
	CHECK(!String("x").IsEmpty());
	CHECK(String("hello").IsAscii());
	CHECK(!String("héllo").IsAscii());
	CHECK(String("héllo").IsValidUtf8());
	CHECK(String("123").IsNumeric());
	CHECK(String("-42").IsNumeric());
	CHECK(String("3.14").IsNumeric());
	CHECK(!String("12x").IsNumeric());
	CHECK(String("abc").IsAlpha());
	CHECK(!String("ab1").IsAlpha());
	CHECK(String("ab1").IsAlnum());
	CHECK(String("  \t\n").IsWhitespace());
}

TEST(NumericConversion) {
	CHECK_EQ(String("42").ToInt32(), 42);
	CHECK_EQ(String("-7").ToInt32(), -7);
	CHECK(String("3.14").ToFloat() > 3.13f);
	CHECK(String("2.71828").ToDouble() > 2.718);
	CHECK_EQ(String("9876543210").ToInt64(), 9876543210LL);

	auto ok = String("99").TryParseInt();
	CHECK(ok.IsSome());
	CHECK_EQ(*ok, 99);

	auto bad = String("9x9").TryParseInt();
	CHECK(!bad.IsSome());

	auto dok = String("1.5").TryParseDouble();
	CHECK(dok.IsSome());
	CHECK(*dok > 1.4);

	auto dbad = String("x").TryParseDouble();
	CHECK(!dbad.IsSome());

	CHECK_EQ(String("55").ToInt32(), 55);
	CHECK(String("1.5").ToFloat() > 1.4f);
}

TEST(Unicode) {
	String s(u8"Héllo 🌍");

	CHECK(s.IsValidUtf8());
	CHECK(s.GetSize() > s.ULength());
	CHECK_EQ(s.ULength(), 7);

	size_t n = 0;
	for ([[maybe_unused]] auto cp : s.Codepoints())
		++n;
	CHECK_EQ(n, 7);

	auto cps = s.ToCodepoints();
	CHECK_EQ(cps.size(), 7);
	CHECK_EQ(cps[1], 0x00E9);
	CHECK_EQ(cps[6], 0x1F30D);

	CHECK_EQ(s.UCharAt(0), 'H');
	CHECK_EQ(s.UCharAt(1), 0x00E9);

	String latin("Héllo");
	CHECK_EQ(latin.UToLower().UCharAt(1), 0x00E9);
	CHECK_EQ(latin.UToUpper().UCharAt(0), 'H');

	std::string ideographicSpace;
	unicode::EncodeUtf8(0x3000, ideographicSpace);
	String padded(ideographicSpace + "hello" + ideographicSpace);
	CHECK_EQ(padded.UTrim(), "hello");

	auto uchars = String(u8"Hé").UChars();
	CHECK_EQ(uchars.size(), 2);
	CHECK_EQ(uchars[1].GetSize(), 2);

	String rev = String(u8"héllo").UReverse();
	CHECK_EQ(rev.GetSize(), String(u8"héllo").GetSize());
	CHECK_EQ(rev.UCharAt(4), 'h');
}

TEST(UtfConversion) {
	String s(u8"Héllo");

	auto u16 = s.ToUtf16();
	CHECK_EQ(u16.size(), 5);
	CHECK_EQ(static_cast<uint32_t>(u16[1]), uint32_t(0x00E9));

	String back16 = String::FromUtf16(u16);
	CHECK_EQ(back16, s);

	auto u32 = s.ToUtf32();
	CHECK_EQ(u32.size(), 5);
	CHECK_EQ(static_cast<uint32_t>(u32[1]), uint32_t(0x00E9));

	String back32 = String::FromUtf32(u32);
	CHECK_EQ(back32, s);

	String emoji(u8"🌍🌎🌏");
	auto u32e = emoji.ToUtf32();
	CHECK_EQ(u32e.size(), 3);
	CHECK_EQ(String::FromUtf32(u32e), emoji);

	auto u16e = emoji.ToUtf16();
	CHECK_EQ(u16e.size(), 6);
	CHECK_EQ(String::FromUtf16(u16e), emoji);
}

TEST(UnicodeValidity) {
	CHECK(unicode::IsValidUtf8("hello"));
	CHECK(unicode::IsValidUtf8(u8"héllo"));
	CHECK(unicode::IsValidUtf8(u8"🌍"));
	CHECK(!unicode::IsValidUtf8("\x80"));
	CHECK(!unicode::IsValidUtf8("\xC0\x80"));
	CHECK(!unicode::IsValidUtf8("\xED\xA0\x80"));

	CHECK_EQ(unicode::CodepointCount("hello"), 5);
	CHECK_EQ(unicode::CodepointCount(u8"héllo"), 5);
	CHECK_EQ(unicode::CodepointCount(u8"🌍🌍"), 2);
}

TEST(StreamHash) {
	std::ostringstream oss;
	oss << String("hello");
	CHECK_EQ(oss.str(), "hello");

	std::istringstream iss("world");
	String s;
	iss >> s;
	CHECK_EQ(s, "world");

	std::istringstream iss2("line1\nline2");
	String l1, l2;
	Getline(iss2, l1);
	Getline(iss2, l2);
	CHECK_EQ(l1, "line1");
	CHECK_EQ(l2, "line2");

	std::unordered_map<String, int> m;
	m["key"_s] = 42;
	CHECK_EQ(m.at("key"_s), 42);

	std::unordered_map<String, int, String::Hash> m2;
	m2[String("k")] = 99;
	CHECK_EQ(m2[String("k")], 99);
}

TEST(ToCharArray) {
	String s("hello world");
	char buf[6];
	s.ToCharArray(buf, sizeof(buf));
	CHECK_EQ(std::string(buf), "hello");

	s.ToCharArray(buf, sizeof(buf), 6);
	CHECK_EQ(std::string(buf), "world");

	uint8_t ubuf[4];
	s.GetBytes(ubuf, sizeof(ubuf));
	CHECK_EQ(ubuf[0], 'h');

	String r;
	r.Reserve(100);
	CHECK(true);
}

TEST(EdgeCases) {
	CHECK_EQ(String("").Split(",").size(), 1);
	CHECK_EQ(String("").Split(",")[0], "");
	CHECK_EQ(String("").Trim(), "");
	CHECK_EQ(String("").Replace("x", "y"), "");
	CHECK_EQ(String("").Repeat(5), "");
	CHECK_EQ(String("").PadLeft(3), "   ");
	CHECK(String::Join({}).IsEmpty());

	auto p = String("abc").Split(",");
	CHECK_EQ(p.size(), 1);
	CHECK_EQ(p[0], "abc");

	CHECK_EQ(String("abc").IndexOf('z'), -1);
	CHECK_EQ(String("a").UCharAt(99), UNICODE_REPLACEMENT);

	String nullStr(static_cast<const char *>(nullptr));
	CHECK(nullStr.IsEmpty());
}
