#pragma once
/**
 * script_bindings — lecture d'une couleur du script, partagé par
 * `render3d` et `ui` (qui en avaient chacun leur copie).
 *
 *   "#rrggbb", "#rrggbbaa"     texte hexadécimal
 *   [r, g, b(, a)]             liste — 0-1, ou 0-255 si une composante > 1
 *   {r: …, g: …, b: …, a: …}   table — mêmes échelles, absentes = 1
 *
 * Ce fichier n'est pas autonome : il suppose `sdl3::Color` / `sdl3::FColor`
 * déjà déclarés (les en-têtes de render3d et ui les apportent).
 */
#include <array>
#include <cstdio>

#include <script/script_std.hpp>
#include <sdl3/structs.hpp>

namespace script {

namespace scriptlib {

using lib::Fail;

/// Composantes 0-1 d'une couleur (règle partagée : si UNE composante dépasse
/// 1, toutes sont traitées comme des octets 0-255).
[[nodiscard]] inline Result<std::array<float, 4>, ScriptError> ColorOf(const Value &v, const char *fn) {
	// "#rrggbb" / "#rrggbbaa"
	if (v.IsString()) {
		const String &text = v.AsString();
		const char *s = text.CStr();
		if (s[0] == '#' && (text.GetSize() == 7 || text.GetSize() == 9)) {
			unsigned value = 0;
			std::array<float, 4> channels = {0.f, 0.f, 0.f, 1.f};
			const int count = text.GetSize() == 9 ? 4 : 3;
			for (int i = 0; i < count; ++i) {
				if (std::sscanf(s + 1 + i * 2, "%2x", &value) != 1)
					return Err(Fail(String::Format("`%s` : couleur `%s` invalide", fn, s)));
				channels[size_t(i)] = float(value) / 255.f;
			}
			return Ok(channels);
		}
		return Err(Fail(String::Format("`%s` : couleur `%s` invalide (attendu #rrggbb ou #rrggbbaa)", fn, s)));
	}
	// [r, g, b(, a)] ou {r, g, b, a}
	std::array<Value, 4> parts = {Value::Number(0.0), Value::Number(0.0), Value::Number(0.0), Value::Number(1.0)};
	bool hasAlpha = false;
	if (v.IsList() && v.AsList() && v.AsList()->Size() >= 3) {
		const std::vector<Value> items = v.AsList()->Snapshot();
		for (size_t i = 0; i < 4 && i < items.size(); ++i)
			parts[i] = items[i];
		hasAlpha = items.size() >= 4;
	} else if (v.IsMap() && v.AsMap()) {
		parts[0] = v.AsMap()->Get(String("r")).UnwrapOr(Value::Number(0.0));
		parts[1] = v.AsMap()->Get(String("g")).UnwrapOr(Value::Number(0.0));
		parts[2] = v.AsMap()->Get(String("b")).UnwrapOr(Value::Number(0.0));
		Option<Value> alpha = v.AsMap()->Get(String("a"));
		hasAlpha = alpha.IsSome();
		parts[3] = alpha.UnwrapOr(Value::Number(1.0));
	} else {
		return Err(Fail(String::Format("`%s` : couleur attendue (\"#rrggbb\", [r, g, b] ou {r, g, b, a}), trouvé `%s`", fn,
									   v.TypeName())));
	}
	std::array<float, 4> channels = {};
	bool bytes = false;
	for (size_t i = 0; i < 4; ++i) {
		if (!parts[i].IsNumber())
			return Err(Fail(String::Format("`%s` : composante de couleur non numérique", fn)));
		channels[i] = parts[i].AsFloat();
		bytes = bytes || channels[i] > 1.f;
	}
	if (bytes)
		for (float &channel : channels)
			channel /= 255.f;
	// Alpha absent : opaque, quelle que soit l'échelle (0-1 ou 0-255) des
	// composantes fournies — sinon [0, 255, 0] deviendrait presque transparent.
	if (!hasAlpha)
		channels[3] = 1.f;
	return Ok(channels);
}

/// Couleur flottante 0-1 (celle de `ui`).
[[nodiscard]] inline Result<sdl3::FColor, ScriptError> FloatColor(const Value &v, const char *fn) {
	auto parsed = ColorOf(v, fn);
	if (parsed.IsError())
		return Err(parsed.Error());
	const std::array<float, 4> &c = parsed.Value();
	return Ok(sdl3::FColor{c[0], c[1], c[2], c[3]});
}

/// Couleur 8 bits, composantes bornées à [0, 255] (celle de `render3d`).
[[nodiscard]] inline Result<sdl3::Color, ScriptError> ByteColor(const Value &v, const char *fn) {
	auto parsed = ColorOf(v, fn);
	if (parsed.IsError())
		return Err(parsed.Error());
	auto to8 = [](float f) { return uint8_t(std::lround(std::clamp(f, 0.f, 1.f) * 255.f)); };
	const std::array<float, 4> &c = parsed.Value();
	return Ok(sdl3::Color(to8(c[0]), to8(c[1]), to8(c[2]), to8(c[3])));
}

} // namespace scriptlib

} // namespace script
