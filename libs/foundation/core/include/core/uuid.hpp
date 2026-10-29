#pragma once
/**
 * Uuid — identifiant STABLE de 128 bits (RFC 4122).
 *
 * Ce qui doit survivre à une sauvegarde, une recharge, une annulation : un
 * asset (AssetId), une définition d'objet, un nœud persistant. Un pointeur ne
 * survit pas à la destruction de ce qu'il désigne ; un indice change quand on
 * réordonne ; un Uuid, lui, reste — c'est par lui que les références sont
 * écrites dans les fichiers puis résolues en mémoire.
 *
 *  - `Generate()` : aléatoire (version 4) ;
 *  - `FromName(espace, nom)` : déterministe (même nom → même Uuid, version 5
 *    à la façon de la RFC, sur un hachage de 128 bits non cryptographique) —
 *    l'id d'un asset importé depuis son chemin, d'une primitive depuis ses
 *    paramètres ;
 *  - forme texte canonique `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`.
 */
#include <cstdint>
#include <functional>

#include "option.hpp"
#include "string.hpp"
#include "string_view.hpp"

struct Uuid {
	uint64_t high = 0;
	uint64_t low = 0;

	/// L'Uuid nul (« aucun »).
	[[nodiscard]] static constexpr Uuid Nil() noexcept { return Uuid{}; }

	/// Uuid aléatoire (version 4, variante RFC 4122).
	[[nodiscard]] static Uuid Generate();

	/// Uuid déterministe dérivé d'un espace de noms et d'un nom (version 5).
	[[nodiscard]] static Uuid FromName(const Uuid &space, StringView name) noexcept;

	/// Analyse la forme canonique (tirets facultatifs, casse indifférente).
	[[nodiscard]] static Option<Uuid> Parse(StringView text) noexcept;

	/// Forme canonique en minuscules.
	[[nodiscard]] String ToString() const;

	[[nodiscard]] constexpr bool IsNil() const noexcept { return high == 0 && low == 0; }
	[[nodiscard]] constexpr explicit operator bool() const noexcept { return !IsNil(); }
	[[nodiscard]] constexpr int Version() const noexcept { return int((high >> 12) & 0xF); }

	[[nodiscard]] constexpr bool operator==(const Uuid &o) const noexcept { return high == o.high && low == o.low; }
	[[nodiscard]] constexpr bool operator!=(const Uuid &o) const noexcept { return !(*this == o); }
	[[nodiscard]] constexpr bool operator<(const Uuid &o) const noexcept {
		return high != o.high ? high < o.high : low < o.low;
	}
};

template <> struct std::hash<Uuid> {
	size_t operator()(const Uuid &id) const noexcept { return size_t(id.high ^ (id.low * 0x9E3779B97F4A7C15ull)); }
};
