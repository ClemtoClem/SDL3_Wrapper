#define USE_TEST

#include "core/core.hpp"
#include <sstream>
#include <unordered_map>

using namespace string_literals;

// ===========================================================================
// SUITE : StringTest
// ===========================================================================

TEST(StringTest, Construction) {
	EXPECT_TRUE(String().IsEmpty());
	EXPECT_EQ(String("hello"), "hello");
	EXPECT_EQ(String(std::string("hi")), "hi");
	EXPECT_EQ(String(StringView("sv")), "sv");
	EXPECT_EQ(String('A'), "A");
	EXPECT_EQ(String(3, 'x'), "xxx");
	EXPECT_EQ("world"_s, "world");
	EXPECT_EQ(String(String("abc")), "abc");

	String s1;
	s1 = "xyz";
	EXPECT_EQ(s1, "xyz");

	String s2;
	s2 = StringView("sv2");
	EXPECT_EQ(s2, "sv2");
}

TEST(StringTest, Factories) {
	EXPECT_EQ(String::From(42), "42");
	EXPECT_EQ(String::From(10, 2), "1010");
	EXPECT_EQ(String::From(255, 16), "ff");
	EXPECT_EQ(String::From(-7), "-7");
	EXPECT_EQ(String::From(3.14f, 2), "3.14");
	EXPECT_EQ(String::From(2.71828, 3), "2.718");
	EXPECT_EQ(String::From(true), "true");
	EXPECT_EQ(String::From(false), "false");
	EXPECT_EQ(String::Format("%d+%d=%d", 1, 2, 3), "1+2=3");
	EXPECT_EQ(String::Format("%.1f", 3.14), "3.1");
}

TEST(StringTest, Access) {
	String s("hello");
	EXPECT_EQ(s.GetSize(), 5);
	EXPECT_EQ(s.Length(), 5);
	EXPECT_FALSE(s.IsEmpty());
	EXPECT_EQ(s[0], 'h');
	EXPECT_EQ(s.CharAt(4), 'o');
	EXPECT_EQ(s.Front(), 'h');
	EXPECT_EQ(s.Back(), 'o');
	EXPECT_EQ(std::string(s.CStr()), "hello");
	EXPECT_EQ(s.View(), "hello");

	String e;
	EXPECT_TRUE(e.IsEmpty());
	EXPECT_TRUE(!e);
	EXPECT_TRUE(!!s);
}

TEST(StringTest, Comparison) {
	String a("abc"), b("abc"), c("xyz");
	EXPECT_TRUE(a == b);
	EXPECT_TRUE(a == "abc");
	EXPECT_TRUE(a != c);
	EXPECT_TRUE(a < c);
	EXPECT_TRUE(c > a);
	EXPECT_TRUE(a <= b);
	EXPECT_TRUE(a >= b);
	EXPECT_EQ(a.Compare(b), 0);
	EXPECT_TRUE(a.Compare(c) < 0);
	EXPECT_TRUE(a.Equals("abc"));
	EXPECT_TRUE(String("Hello").EqualsIgnoreCase(String("hELLO")));
	EXPECT_FALSE(String("abc").EqualsIgnoreCase(String("abcd")));
}

TEST(StringTest, Search) {
	String s("hello world hello");
	EXPECT_EQ(s.Find('o'), 4);
	EXPECT_EQ(s.Find("world"), 6);
	EXPECT_EQ(s.Find('o', 5), 7);
	EXPECT_EQ(s.Rfind('o'), 16);
	EXPECT_EQ(s.Rfind("hello"), 12);
	EXPECT_EQ(s.IndexOf('o'), 4);
	EXPECT_EQ(s.IndexOf(String("world")), 6);
	EXPECT_EQ(s.LastIndexOf('o'), 16);
	EXPECT_TRUE(s.Contains("world"));
	EXPECT_FALSE(s.Contains("xyz"));
	EXPECT_TRUE(s.StartsWith("hello"));
	EXPECT_TRUE(s.EndsWith("hello"));
	EXPECT_TRUE(s.StartsWith(String("world"), 6));
	EXPECT_TRUE(s.EndsWith(String("hello")));
	EXPECT_EQ(s.Count('l'), 5);
	EXPECT_EQ(s.Count("hello"), 2);
	EXPECT_EQ(s.Find("zzz"), String::NPOS);
}

TEST(StringTest, Extraction) {
	String s("hello world");
	EXPECT_EQ(s.Substr(6), "world");
	EXPECT_EQ(s.Substr(0, 5), "hello");
	EXPECT_EQ(s.Substring(6, 11), "world");
	EXPECT_EQ(s.Substring(6), "world");

	auto between = String("<tag>content</tag>").GetBetween("<tag>", "</tag>");
	EXPECT_TRUE(between.IsSome());
	EXPECT_EQ(*between, "content");

	auto none = String("no tags").GetBetween("<", ">");
	EXPECT_FALSE(none.IsSome());
}

TEST(StringTest, Concat) {
	String s("foo");
	s += "bar";
	EXPECT_EQ(s, "foobar");
	s += 42;
	EXPECT_EQ(s, "foobar42");
	s += 3.14;
	EXPECT_TRUE(s.StartsWith("foobar42"));

	String a = "a"_s + "b"_s + "c"_s;
	EXPECT_EQ(a, "abc");
	EXPECT_EQ(String("x") + "yz", "xyz");
	EXPECT_TRUE(std::string("pre") + "fix" == "prefix");

	String c;
	c.Concat("hello");
	c.Concat(' ');
	c.Concat(42);
	EXPECT_TRUE(c.StartsWith("hello "));

	String b("x");
	b.Append("y").Append("z").Append(size_t(1), '!');
	EXPECT_EQ(b, "xyz!");
}

TEST(StringTest, Transform) {
	EXPECT_EQ(String("HÉLLO").ToLower(), "hÉllo");
	EXPECT_EQ(String("hello").ToUpper(), "HELLO");
	EXPECT_EQ(String("HELLO").UToLower(), "hello");
	EXPECT_EQ(String("hello").UToUpper(), "HELLO");

	String t("  hello  ");
	EXPECT_EQ(t.Trim(), "hello");
	EXPECT_EQ(t.TrimLeft(), "hello  ");
	EXPECT_EQ(t.TrimRight(), "  hello");

	EXPECT_EQ(String("aabbcc").Replace("bb", "XX"), "aaXXcc");
	EXPECT_EQ(String("aaa").Replace('a', 'b'), "bbb");
	EXPECT_EQ(String("abc").Replace("x", "y"), "abc");

	EXPECT_EQ(String("hello").Remove(1, 3), "ho");
	EXPECT_EQ(String("helo").Insert(3, String("l")), "hello");
	EXPECT_EQ(String("hllo").Insert(1, 'e'), "hello");

	EXPECT_EQ(String("ab").Repeat(3), "ababab");
	EXPECT_EQ(String("ab").Repeat(0), "");
	EXPECT_EQ(String("abc").ReverseBytes(), "cba");

	EXPECT_EQ(String("42").PadLeft(5), "   42");
	EXPECT_EQ(String("42").PadRight(5), "42   ");
	EXPECT_EQ(String("hi").PadCenter(6), "  hi  ");
	EXPECT_EQ(String("7").PadLeft(3, '0'), "007");
	EXPECT_EQ(String("hello").Truncate(3), "hel");
	EXPECT_EQ(String("hi").Truncate(10), "hi");
}

TEST(StringTest, SplitJoin) {
	auto parts = String("a,b,c").Split(",");
	EXPECT_EQ(parts.size(), 3);
	EXPECT_EQ(parts[0], "a");
	EXPECT_EQ(parts[2], "c");

	auto limited = String("a:b:c:d").Split(":", 2);
	EXPECT_EQ(limited.size(), 3);
	EXPECT_EQ(limited[2], "c:d");

	auto any = String("a,b;c").SplitAny(",;");
	EXPECT_EQ(any.size(), 3);

	auto ls = String("foo\nbar\r\nbaz").Lines();
	EXPECT_EQ(ls.size(), 3);
	EXPECT_EQ(ls[0], "foo");
	EXPECT_EQ(ls[2], "baz");

	auto ch = String("hi").Chars();
	EXPECT_EQ(ch.size(), 2);
	EXPECT_EQ(ch[0], "h");

	EXPECT_EQ(String::Join({"a", "b", "c"}, "-"), "a-b-c");
	EXPECT_EQ(String::Join({"x", "y"}), "xy");
	EXPECT_EQ(String::Join(String("1,2,3").Split(","), "|"), "1|2|3");
}

TEST(StringTest, Functional) {
	String r =
		String("hello").Map([](CodepointT cp) -> CodepointT { return (cp >= 'a' && cp <= 'z') ? cp - 32 : cp; });
	EXPECT_EQ(r, "HELLO");

	String digits = String("a1b2c3").Filter([](CodepointT cp) { return cp >= '0' && cp <= '9'; });
	EXPECT_EQ(digits, "123");

	String doubled = String("abc").FlatMap([](CodepointT cp) -> String {
		std::string s;
		s += static_cast<char>(cp);
		s += static_cast<char>(cp);
		return String(s);
	});
	EXPECT_EQ(doubled, "aabbcc");

	size_t total = String("hello").Reduce(size_t(0), [](size_t acc, CodepointT) { return acc + 1; });
	EXPECT_EQ(total, 5);

	EXPECT_TRUE(String("abc3").Any([](CodepointT cp) { return cp >= '0' && cp <= '9'; }));
	EXPECT_FALSE(String("abc").Any([](CodepointT cp) { return cp >= '0' && cp <= '9'; }));
	EXPECT_TRUE(String("abc").All([](CodepointT cp) { return cp >= 'a' && cp <= 'z'; }));
	EXPECT_FALSE(String("aBc").All([](CodepointT cp) { return cp >= 'a' && cp <= 'z'; }));

	auto shout = [](String s) { return s.ToUpper().Append("!"); };
	auto trim = [](String s) { return s.Trim(); };
	String piped = String("  hello  ") | trim | shout;
	EXPECT_EQ(piped, "HELLO!");
}

TEST(StringTest, Predicates) {
	EXPECT_TRUE(String("").IsEmpty());
	EXPECT_FALSE(String("x").IsEmpty());
	EXPECT_TRUE(String("hello").IsAscii());
	EXPECT_FALSE(String("héllo").IsAscii());
	EXPECT_TRUE(String("héllo").IsValidUtf8());
	EXPECT_TRUE(String("123").IsNumeric());
	EXPECT_TRUE(String("-42").IsNumeric());
	EXPECT_TRUE(String("3.14").IsNumeric());
	EXPECT_FALSE(String("12x").IsNumeric());
	EXPECT_TRUE(String("abc").IsAlpha());
	EXPECT_FALSE(String("ab1").IsAlpha());
	EXPECT_TRUE(String("ab1").IsAlnum());
	EXPECT_TRUE(String("  \t\n").IsWhitespace());
}

TEST(StringTest, NumericConversion) {
	EXPECT_EQ(String("42").ToInt32(), 42);
	EXPECT_EQ(String("-7").ToInt32(), -7);
	EXPECT_TRUE(String("3.14").ToFloat() > 3.13f);
	EXPECT_TRUE(String("2.71828").ToDouble() > 2.718);
	EXPECT_EQ(String("9876543210").ToInt64(), 9876543210LL);

	auto ok = String("99").TryParseInt();
	EXPECT_TRUE(ok.IsSome());
	EXPECT_EQ(*ok, 99);

	auto bad = String("9x9").TryParseInt();
	EXPECT_FALSE(bad.IsSome());

	auto dok = String("1.5").TryParseDouble();
	EXPECT_TRUE(dok.IsSome());
	EXPECT_TRUE(*dok > 1.4);

	auto dbad = String("x").TryParseDouble();
	EXPECT_FALSE(dbad.IsSome());

	EXPECT_EQ(String("55").ToInt32(), 55);
	EXPECT_TRUE(String("1.5").ToFloat() > 1.4f);
}

TEST(StringTest, Unicode) {
	String s(u8"Héllo 🌍");

	EXPECT_TRUE(s.IsValidUtf8());
	EXPECT_TRUE(s.GetSize() > s.ULength());
	EXPECT_EQ(s.ULength(), 7);

	size_t n = 0;
	for ([[maybe_unused]] auto cp : s.Codepoints())
		++n;
	EXPECT_EQ(n, 7);

	auto cps = s.ToCodepoints();
	EXPECT_EQ(cps.size(), 7);
	EXPECT_EQ(cps[1], 0x00E9);
	EXPECT_EQ(cps[6], 0x1F30D);

	EXPECT_EQ(s.UCharAt(0), 'H');
	EXPECT_EQ(s.UCharAt(1), 0x00E9);

	String latin("Héllo");
	EXPECT_EQ(latin.UToLower().UCharAt(1), 0x00E9);
	EXPECT_EQ(latin.UToUpper().UCharAt(0), 'H');

	std::string ideographicSpace;
	unicode::EncodeUtf8(0x3000, ideographicSpace);
	String padded(ideographicSpace + "hello" + ideographicSpace);
	EXPECT_EQ(padded.UTrim(), "hello");

	auto uchars = String(u8"Hé").UChars();
	EXPECT_EQ(uchars.size(), 2);
	EXPECT_EQ(uchars[1].GetSize(), 2);

	String rev = String(u8"héllo").UReverse();
	EXPECT_EQ(rev.GetSize(), String(u8"héllo").GetSize());
	EXPECT_EQ(rev.UCharAt(4), 'h');
}

TEST(StringTest, UtfConversion) {
	String s(u8"Héllo");

	auto u16 = s.ToUtf16();
	EXPECT_EQ(u16.size(), 5);
	EXPECT_EQ(static_cast<uint32_t>(u16[1]), uint32_t(0x00E9));

	String back16 = String::FromUtf16(u16);
	EXPECT_EQ(back16, s);

	auto u32 = s.ToUtf32();
	EXPECT_EQ(u32.size(), 5);
	EXPECT_EQ(static_cast<uint32_t>(u32[1]), uint32_t(0x00E9));

	String back32 = String::FromUtf32(u32);
	EXPECT_EQ(back32, s);

	String emoji(u8"🌍🌎🌏");
	auto u32e = emoji.ToUtf32();
	EXPECT_EQ(u32e.size(), 3);
	EXPECT_EQ(String::FromUtf32(u32e), emoji);

	auto u16e = emoji.ToUtf16();
	EXPECT_EQ(u16e.size(), 6);
	EXPECT_EQ(String::FromUtf16(u16e), emoji);
}

TEST(StringTest, UnicodeValidity) {
	EXPECT_TRUE(unicode::IsValidUtf8("hello"));
	EXPECT_TRUE(unicode::IsValidUtf8(u8"héllo"));
	EXPECT_TRUE(unicode::IsValidUtf8(u8"🌍"));
	EXPECT_FALSE(unicode::IsValidUtf8("\x80"));
	EXPECT_FALSE(unicode::IsValidUtf8("\xC0\x80"));
	EXPECT_FALSE(unicode::IsValidUtf8("\xED\xA0\x80"));

	EXPECT_EQ(unicode::CodepointCount("hello"), 5);
	EXPECT_EQ(unicode::CodepointCount(u8"héllo"), 5);
	EXPECT_EQ(unicode::CodepointCount(u8"🌍🌍"), 2);
}

TEST(StringTest, StreamHash) {
	std::ostringstream oss;
	oss << String("hello");
	EXPECT_EQ(oss.str(), "hello");

	std::istringstream iss("world");
	String s;
	iss >> s;
	EXPECT_EQ(s, "world");

	std::istringstream iss2("line1\nline2");
	String l1, l2;
	Getline(iss2, l1);
	Getline(iss2, l2);
	EXPECT_EQ(l1, "line1");
	EXPECT_EQ(l2, "line2");

	std::unordered_map<String, int> m;
	m["key"_s] = 42;
	EXPECT_EQ(m.at("key"_s), 42);

	std::unordered_map<String, int, String::Hash> m2;
	m2[String("k")] = 99;
	EXPECT_EQ(m2[String("k")], 99);
}

TEST(StringTest, ToCharArray) {
	String s("hello world");
	char buf[6];
	s.ToCharArray(buf, sizeof(buf));
	EXPECT_EQ(std::string(buf), "hello");

	s.ToCharArray(buf, sizeof(buf), 6);
	EXPECT_EQ(std::string(buf), "world");

	uint8_t ubuf[4];
	s.GetBytes(ubuf, sizeof(ubuf));
	EXPECT_EQ(ubuf[0], 'h');

	String r;
	r.Reserve(100);
	EXPECT_TRUE(true);
}

TEST(StringTest, EdgeCases) {
	EXPECT_EQ(String("").Split(",").size(), 1);
	EXPECT_EQ(String("").Split(",")[0], "");
	EXPECT_EQ(String("").Trim(), "");
	EXPECT_EQ(String("").Replace("x", "y"), "");
	EXPECT_EQ(String("").Repeat(5), "");
	EXPECT_EQ(String("").PadLeft(3), "   ");
	EXPECT_TRUE(String::Join({}).IsEmpty());

	auto p = String("abc").Split(",");
	EXPECT_EQ(p.size(), 1);
	EXPECT_EQ(p[0], "abc");

	EXPECT_EQ(String("abc").IndexOf('z'), -1);
	EXPECT_EQ(String("a").UCharAt(99), UNICODE_REPLACEMENT);

	String nullStr(static_cast<const char *>(nullptr));
	EXPECT_TRUE(nullStr.IsEmpty());
}

int main() {
	return RUN_ALL_TESTS();
}