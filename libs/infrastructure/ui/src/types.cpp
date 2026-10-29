// Définitions de ui/types.hpp
#include <ui/types.hpp>

#include <cmath>

namespace ui {

Color HsvToColor(float h, float s, float v, float a) noexcept {
	h = std::fmod(h, 360.f);
	if (h < 0.f)
		h += 360.f;
	s = math::Clamp(s, 0.f, 1.f);
	v = math::Clamp(v, 0.f, 1.f);
	const float c = v * s;
	const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
	const float m = v - c;
	float r = 0.f, g = 0.f, b = 0.f;
	if (h < 60.f) {
		r = c;
		g = x;
	} else if (h < 120.f) {
		r = x;
		g = c;
	} else if (h < 180.f) {
		g = c;
		b = x;
	} else if (h < 240.f) {
		g = x;
		b = c;
	} else if (h < 300.f) {
		r = x;
		b = c;
	} else {
		r = c;
		b = x;
	}
	return Color{r + m, g + m, b + m, a};
}

Hsv ColorToHsv(Color c) noexcept {
	const float mx = math::Max(c.r, math::Max(c.g, c.b));
	const float mn = math::Min(c.r, math::Min(c.g, c.b));
	const float d = mx - mn;
	Hsv out;
	out.v = mx;
	out.s = mx > 0.f ? d / mx : 0.f;
	if (d <= 0.f)
		return out;
	if (mx == c.r)
		out.h = 60.f * std::fmod((c.g - c.b) / d, 6.f);
	else if (mx == c.g)
		out.h = 60.f * ((c.b - c.r) / d + 2.f);
	else
		out.h = 60.f * ((c.r - c.g) / d + 4.f);
	if (out.h < 0.f)
		out.h += 360.f;
	return out;
}

String ColorToHex(Color c) {
	auto byte = [](float f) { return unsigned(std::lround(math::Clamp(f, 0.f, 1.f) * 255.f)); };
	return String::Format("#%02X%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b), byte(c.a));
}

namespace {

int HexDigit(char c) noexcept {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return 10 + c - 'a';
	if (c >= 'A' && c <= 'F')
		return 10 + c - 'A';
	return -1;
}

} // namespace

Option<Color> ParseHexColor(StringView hex) noexcept {
	if (!hex.IsEmpty() && hex[0] == '#')
		hex = hex.Substr(1);
	const size_t n = hex.GetSize();
	if (n != 3 && n != 4 && n != 6 && n != 8)
		return NONE;
	int v[8] = {};
	for (size_t i = 0; i < n; ++i) {
		v[i] = HexDigit(hex[i]);
		if (v[i] < 0)
			return NONE;
	}
	auto ch = [](int b) { return float(b) / 255.f; };
	if (n == 3 || n == 4) {
		const int a = n == 4 ? v[3] * 17 : 255;
		return Some(Color{ch(v[0] * 17), ch(v[1] * 17), ch(v[2] * 17), ch(a)});
	}
	const int a = n == 8 ? v[6] * 16 + v[7] : 255;
	return Some(Color{ch(v[0] * 16 + v[1]), ch(v[2] * 16 + v[3]), ch(v[4] * 16 + v[5]), ch(a)});
}

} // namespace ui
