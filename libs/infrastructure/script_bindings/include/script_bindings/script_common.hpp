#pragma once
/**
 * script_bindings — outils communs à plusieurs bibliothèques (espace
 * `scriptlib`).
 *
 * Ce que audio, gen, physics, render3d, ui et archive écrivaient chacun en
 * double : la lecture typée d'une table d'options (avec refus des clés
 * inconnues — une faute de frappe doit être une erreur, pas un silence), la
 * conversion listes ⇄ vecteurs et octets ⇄ listes. Les couleurs, qui ont
 * besoin de `sdl3::Color`, sont dans script_color.hpp, inclus par les
 * seuls modules qui les emploient.
 *
 * Les conversions de VECTEURS en lecture s'appuient sur
 * `mathlib::Vec3Of` (cf. script_math.hpp) : `math.vec3` du script, liste
 * `[x, y, z]` ou table `{x: …, y: …, z: …}` — un surensemble des lecteurs
 * locales qu'il remplace.
 */
#include <cstdint>
#include <vector>

#include <script/script_std.hpp>

namespace script {

namespace scriptlib {

using lib::Args;
using lib::Fail;

// ============================================================================
// Lecture d'une table d'options
// ============================================================================

/**
 * Options d'une table `{clé: valeur, …}` : lecture typée, puis refus des
 * clés inconnues (`Finish()`) — chaque lecteur pose les clés qu'il
 * connaît, une clé non réclamée est une faute de frappe signalée.
 *
 *   scriptlib::Options o(args, 2, "audio.play");   // 2ᵉ argument, facultatif
 *   const float volume = o.Float("volume", 1.f);
 *   const bool boucle = o.Bool("loop", false);
 *   if (auto error = o.Finish(); error.IsSome())
 *       return Err(error.Unwrap());
 *
 * Toutes les lectures accumulent la PREMIÈRE erreur et continuent : un
 * seul `Finish()` à la fin suffit, le message est celui de la première
 * faute. L'argument absent (`index >= args.size()`), `nil`, ou une table
 * vide signifient « aucune option » ; autre chose qu'une table est une
 * erreur dès le constructeur.
 */
class Options {
public:
	Options(const Value &value, const char *fn) : m_fn(fn) {
		if (value.IsMap() && value.AsMap())
			m_entries = value.AsMap()->Snapshot();
		else if (!value.IsNil())
			Fault(String::Format("`%s` : table d'options attendue, trouvé `%s`", fn, value.TypeName()));
	}
	/// Dernier argument d'une native, facultatif : `Options o(args, 2, "fn")`.
	Options(const Args &args, size_t index, const char *fn) : m_fn(fn) {
		if (index >= args.size() || args[index].IsNil())
			return;
		if (args[index].IsMap() && args[index].AsMap())
			m_entries = args[index].AsMap()->Snapshot();
		else
			Fault(String::Format("`%s` : table d'options attendue, trouvé `%s`", fn, args[index].TypeName()));
	}

	/// Valeur brute d'une clé (la marque comme connue), NONE si absente.
	[[nodiscard]] Option<Value> Get(const char *key) {
		m_known.push_back(String(key));
		for (const auto &entry : m_entries)
			if (entry.first == key)
				return Some(entry.second);
		return NONE;
	}

	/// Nombre ; l'absence rend `fallback`, le non-numérique est une faute.
	[[nodiscard]] double Number(const char *key, double fallback) {
		Option<Value> v = Get(key);
		if (v.IsNone())
			return fallback;
		if (!v.Value().IsNumber()) {
			Fault(String::Format("`%s` : option `%s` : nombre attendu", m_fn, key));
			return fallback;
		}
		return v.Value().AsNumber();
	}
	[[nodiscard]] float Float(const char *key, float fallback) { return float(Number(key, double(fallback))); }
	[[nodiscard]] int Int(const char *key, int fallback) { return int(Number(key, double(fallback))); }
	/// Entier 64 bits (CRC, tailles, dates) : tronqué comme `Value::AsInt64`.
	[[nodiscard]] int64_t Int64(const char *key, int64_t fallback) { return int64_t(Number(key, double(fallback))); }

	/// Vérité façon Lua : `nil` absent rend `fallback`, sinon `IsTruthy`.
	[[nodiscard]] bool Bool(const char *key, bool fallback) {
		Option<Value> v = Get(key);
		return v.IsNone() ? fallback : v.Value().IsTruthy();
	}

	/// Texte : toute valeur est acceptée (ce qu'affiche `print`).
	[[nodiscard]] String Text(const char *key, const char *fallback) {
		Option<Value> v = Get(key);
		return v.IsNone() ? String(fallback) : v.Value().ToDisplayString();
	}
	/// Texte facultatif : NONE si absent, sinon la valeur affichée.
	[[nodiscard]] Option<String> Str(const char *key) {
		Option<Value> v = Get(key);
		return v.IsNone() ? Option<String>(NONE) : Option<String>(Some(v.Value().ToDisplayString()));
	}

	/// Position [x, y, z] (ou `math.vec3`, ou table {x, y, z}) ; NONE si absente.
	[[nodiscard]] Option<math::FVector3> Vec3(const char *key) {
		Option<Value> v = Get(key);
		if (v.IsNone())
			return NONE;
		auto parsed = mathlib::Vec3Of(v.Value(), m_fn);
		if (parsed.IsError()) {
			// Le nom de l'option d'abord : c'est elle que l'auteur doit corriger.
			Fault(String::Format("`%s` : option `%s` : liste [x, y, z] attendue (%s)", m_fn, key,
								 parsed.Error().message.CStr()));
			return NONE;
		}
		return Some(parsed.Unwrap());
	}

	/// Note une erreur (la première gagne) et continue.
	void Fault(String message) {
		if (m_error.IsNone())
			m_error = Some(Fail(std::move(message)));
	}

	/// Erreur de lecture, ou clé inconnue — À APPELER après toutes les lectures.
	[[nodiscard]] Option<ScriptError> Finish() const {
		if (m_error.IsSome())
			return m_error;
		for (const auto &entry : m_entries)
			if (std::find(m_known.begin(), m_known.end(), entry.first) == m_known.end())
				return Some(Fail(String::Format("`%s` : option `%s` inconnue", m_fn, entry.first.CStr())));
		return NONE;
	}

private:
	const char *m_fn;
	std::vector<std::pair<String, Value>> m_entries;
	std::vector<String> m_known;
	Option<ScriptError> m_error = NONE;
};

// ============================================================================
// Vecteurs
// ============================================================================

/// Lecture d'un vec3 : `math.vec3` du script, liste [x, y, z] ou table
/// {x, y, z} (cf. mathlib::Vec3Of, script_math.hpp — même implémentation,
/// espace de noms unifié pour les bibliothèques).
[[nodiscard]] inline Result<math::FVector3, ScriptError> Vec3Of(const Value &v, const char *fn) {
	return mathlib::Vec3Of(v, fn);
}

// (Conversion vers le script : les listes ci-dessous ; sens inverse,
// `mathlib::QuatOf` / `mathlib::MakeQuat`, cf. script_math.hpp.)

/// Position `[x, y, z]` — une liste ordinaire, comme les options qui la lisent.
[[nodiscard]] inline Value Vec3List(const math::FVector3 &v) {
	auto list = std::make_shared<ListObject>();
	list->items = {Value::Number(v.x), Value::Number(v.y), Value::Number(v.z)};
	return Value::List(std::move(list));
}

/// Rotation `[x, y, z, w]` (quaternion).
[[nodiscard]] inline Value QuatList(const math::FQuaternion &q) {
	auto list = std::make_shared<ListObject>();
	list->items = {Value::Number(q.x), Value::Number(q.y), Value::Number(q.z), Value::Number(q.w)};
	return Value::List(std::move(list));
}

// ============================================================================
// Octets : conversion vers et depuis le script
// ============================================================================

/// Octets → liste d'entiers 0-255. Pour un gros contenu, préférer une
/// écriture sur disque (`extract`) : la liste vit entière en mémoire.
[[nodiscard]] inline Value BytesList(const std::vector<uint8_t> &bytes) {
	auto list = std::make_shared<ListObject>();
	list->items.reserve(bytes.size());
	for (uint8_t b : bytes)
		list->items.push_back(Value::Int(int64_t(b)));
	return Value::List(std::move(list));
}

/// Liste d'entiers 0-255 (ou texte brut) → octets. Hors bornes = erreur.
[[nodiscard]] inline Result<std::vector<uint8_t>, ScriptError> BytesOf(const Value &v, const char *fn) {
	std::vector<uint8_t> out;
	if (v.IsString()) {
		const String &text = v.AsString();
		out.assign(text.CStr(), text.CStr() + text.GetSize());
		return Ok(std::move(out));
	}
	if (v.IsList() && v.AsList()) {
		const std::vector<Value> items = v.AsList()->Snapshot();
		out.reserve(items.size());
		for (const Value &item : items) {
			if (!item.IsNumber())
				return Err(Fail(String::Format("`%s` : liste d'octets attendue, élément `%s` trouvé", fn,
											   item.TypeName())));
			const double n = item.AsNumber();
			if (n < 0 || n > 255)
				return Err(Fail(String::Format("`%s` : octet hors bornes (%s)", fn,
											   Value::NumberToString(n).CStr())));
			out.push_back(uint8_t(n));
		}
		return Ok(std::move(out));
	}
	return Err(Fail(String::Format("`%s` : liste d'octets ou texte attendu, trouvé `%s`", fn, v.TypeName())));
}

} // namespace scriptlib

} // namespace script
