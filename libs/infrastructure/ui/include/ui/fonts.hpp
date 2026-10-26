#pragma once
/**
 * ui::Fonts — familles de polices de l'interface.
 *
 * Une FAMILLE (« DejaVu Sans », « monospace », « icons-material »…) regroupe
 * jusqu'à quatre variantes (normale, grasse, italique, grasse italique).
 * Les widgets choisissent leur police par le style (`font-family`,
 * `font-weight`, `font-style`, `font-size`) ; la variante la plus proche
 * disponible est retenue (gras italique → gras → italique → normale), puis
 * une copie à la taille demandée est créée à la première demande et gardée.
 *
 * Les familles viennent du code (`Register`, `Load`) ou des feuilles de style
 * (`@font-face`, chargées à la première utilisation). Sans aucune police, la
 * mesure retombe sur une heuristique : tout le module reste utilisable sans
 * SDL_ttf (tests sans fenêtre).
 */
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include <sdl3/ttf.hpp>

#include "css.hpp"
#include "text.hpp"

namespace ui {

class Fonts {
public:
	Fonts() = default;
	Fonts(const Fonts &) = delete;
	Fonts &operator=(const Fonts &) = delete;
	~Fonts();

	/// Enregistre une police (non possédée : elle doit vivre plus longtemps
	/// que l'interface). La première famille enregistrée devient la famille
	/// par défaut.
	void Register(StringView family, sdl3::Font &font, int weight = 400, FontStyle style = FontStyle::Normal);
	/// Enregistre une police dont l'interface prend possession.
	void Register(StringView family, sdl3::Font &&font, int weight = 400, FontStyle style = FontStyle::Normal);
	/// Charge un fichier de police (taille de base `size` points).
	Result<bool, String> Load(StringView family, const String &path, int weight = 400,
							  FontStyle style = FontStyle::Normal, float size = 16.f);
	/// Déclare une police `@font-face` (chargée à la première utilisation).
	void AddFace(const FontFace &face);

	void SetDefaultFamily(StringView family) { m_default = String(family); }
	void SetMonospaceFamily(StringView family) { m_monospace = String(family); }
	[[nodiscard]] const String &DefaultFamily() const noexcept { return m_default; }
	[[nodiscard]] bool HasFamily(StringView family) const;
	[[nodiscard]] bool Empty() const noexcept { return m_families.empty() && m_pending.empty(); }

	/// Police de dessin : famille (alias « monospace », « sans-serif »,
	/// vide = défaut), variante la plus proche, à la taille `size`. nullptr si
	/// aucune police n'est disponible.
	[[nodiscard]] sdl3::Font *Resolve(StringView family, float size, int weight = 400,
									  FontStyle style = FontStyle::Normal);

	/// Mesure d'une ligne normalisée avec cette police (heuristique si nullptr).
	[[nodiscard]] sdl3::FPoint Measure(sdl3::Font *font, const String &line, float size);
	/// Mesure liée à une police : à passer aux fonctions de text.hpp.
	[[nodiscard]] LineMeasure Measurer(StringView family, float size, int weight = 400,
									   FontStyle style = FontStyle::Normal);

	/// Largeur d'une cellule de la police à chasse fixe (éditeur de code).
	[[nodiscard]] float CellWidth(StringView family, float size);

	/// Vide les copies dimensionnées et le cache de mesures (changement de DPI…).
	void ClearCaches();

private:
	struct Variant {
		sdl3::Font *font = nullptr;
		std::unique_ptr<sdl3::Font> owned;
		int weight = 400;
		FontStyle style = FontStyle::Normal;
	};
	struct Family {
		std::vector<Variant> variants;
	};

	[[nodiscard]] Family *FindFamily(StringView family);
	void LoadPending(StringView family);
	[[nodiscard]] sdl3::Font *Sized(sdl3::Font *base, float size);

	std::unordered_map<String, Family> m_families;
	std::vector<FontFace> m_pending;
	String m_default;
	String m_monospace;
	/// Copies dimensionnées, par (police, taille en quarts de point). Déclarées
	/// APRÈS les familles : détruites avant elles.
	std::map<std::pair<const sdl3::Font *, int>, std::unique_ptr<sdl3::Font>> m_sized;
	struct MeasureKey {
		const sdl3::Font *font;
		String text;
		bool operator==(const MeasureKey &o) const { return font == o.font && text == o.text; }
	};
	struct MeasureHash {
		size_t operator()(const MeasureKey &k) const noexcept {
			return std::hash<String>{}(k.text) ^ (std::hash<const void *>{}(k.font) << 1);
		}
	};
	std::unordered_map<MeasureKey, sdl3::FPoint, MeasureHash> m_measures;
	std::unordered_map<const sdl3::Font *, float> m_cellWidths;
};

} // namespace ui
