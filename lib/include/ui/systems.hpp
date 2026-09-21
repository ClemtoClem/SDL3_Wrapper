#pragma once
/**
 * ui::systems — les trois passes du pipeline UI :
 *
 *   LayoutSystem — measure (post-ordre, mémoïsé) + place (pré-ordre) ;
 *                  ne recalcule QUE si marqué dirty ou si la fenêtre a changé
 *                  (dirty-flag à la GTK4 : une UI au repos coûte ~0).
 *   InputSystem  — hover/press/click/drag/focus + callbacks applicatifs.
 *   RenderSystem — dessin via sdl3::Renderer (rects arrondis, dégradés,
 *                  cercles) + texte TTF avec cache d'objets sdl3::Text.
 */
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "../sdl3/events.hpp"
#include "../sdl3/input.hpp"
#include "../sdl3/render.hpp"
#include "../sdl3/ttf.hpp"
#include "components.hpp"
#include "interaction.hpp"
#include "plot.hpp"
#include "render_backend.hpp"
#include "styles.hpp"
#include "viewport3d.hpp"

namespace ui {

// ── Helpers ──────────────────────────────────────────────────────────────────
// (l'intersection et le test de contenance viennent de sdl3::FRect)

[[nodiscard]] inline float ClampOpt(float v, const Option<float> &lo, const Option<float> &hi) noexcept {
	if (lo.IsSome())
		v = sdl3::Max(v, lo.Unwrap());
	if (hi.IsSome())
		v = sdl3::Min(v, hi.Unwrap());
	return v;
}

// ============================================================================
// Auto-scrollbar geometry — barre fine dessinée en surimpression sur le bord
// droit/bas d'un conteneur `clipContent` dont le contenu déborde. Une seule
// source de vérité pour le rect du "pouce" (thumb), utilisée à la fois par
// RenderSystem (dessin) et InputSystem (hit-test + drag), pour qu'ils ne
// puissent jamais diverger.
// ============================================================================

constexpr float K_SCROLLBAR_THICKNESS = 8.f;
constexpr float K_SCROLLBAR_MIN_THUMB = 20.f;

/// Rect (dans l'espace écran) du pouce vertical, ou un rect vide si pas de
/// débordement vertical.
[[nodiscard]] inline sdl3::FRect VScrollbarThumbRect(const UiRect &r, const sdl3::FRect &screen) noexcept {
	float maxY = r.MaxScroll().y;
	if (maxY <= 0.f)
		return {};
	float trackH = screen.h;
	float thumbH = sdl3::Max(K_SCROLLBAR_MIN_THUMB, trackH * screen.h / r.ContentSize().y);
	thumbH = sdl3::Min(thumbH, trackH);
	float t = maxY > 0.f ? r.scroll.y / maxY : 0.f;
	float thumbY = screen.y + t * (trackH - thumbH);
	return {screen.x + screen.w - K_SCROLLBAR_THICKNESS, thumbY, K_SCROLLBAR_THICKNESS, thumbH};
}

/// Rect (dans l'espace écran) du pouce horizontal, ou un rect vide si pas de
/// débordement horizontal.
[[nodiscard]] inline sdl3::FRect HScrollbarThumbRect(const UiRect &r, const sdl3::FRect &screen) noexcept {
	float maxX = r.MaxScroll().x;
	if (maxX <= 0.f)
		return {};
	float trackW = screen.w;
	float thumbW = sdl3::Max(K_SCROLLBAR_MIN_THUMB, trackW * screen.w / r.ContentSize().x);
	thumbW = sdl3::Min(thumbW, trackW);
	float t = maxX > 0.f ? r.scroll.x / maxX : 0.f;
	float thumbX = screen.x + t * (trackW - thumbW);
	return {thumbX, screen.y + screen.h - K_SCROLLBAR_THICKNESS, thumbW, K_SCROLLBAR_THICKNESS};
}

// ============================================================================
// Découpage de texte pour UiInput/UiInputArea — une seule source de vérité
// (heuristique caractère fixe, PAS de mesure TTF réelle) partagée entre
// InputSystem (calcul de UiRect.content pour le scroll/la scrollbar) et
// RenderSystem (dessin) : s'ils mesuraient différemment, la hauteur/largeur
// de contenu ne correspondrait pas aux lignes réellement dessinées. Même
// approximation que LayoutSystem::measureText par défaut.
// ============================================================================

[[nodiscard]] constexpr float CharWidthApprox(float fontSize) noexcept { return fontSize * 0.55f; }
[[nodiscard]] constexpr float LineHeightApprox(float fontSize) noexcept { return fontSize * 1.3f; }

/// Une ligne affichée (enveloppée ou brute) avec son décalage en octets dans
/// le texte d'origine — nécessaire pour la navigation clavier (Haut/Bas,
/// Origine/Fin) et le hit-test souris, qui doivent retrouver une position
/// absolue dans `text` à partir d'une ligne/colonne affichée.
struct TextLine {
	size_t offset;
	String text;
};

/// Découpe `text` en lignes de longueur brute (octets) tenant dans `maxWidth`
/// à `fontSize` (UiInput : word-wrap) — coupe au dernier espace si possible,
/// sinon au caractère pour un "mot" plus long que `maxWidth` à lui seul.
/// `\n` n'a pas de sens particulier ici (UiInput est mono-ligne logique).
[[nodiscard]] inline std::vector<TextLine> WrapText(const String &text, float fontSize, float maxWidth) {
	std::vector<TextLine> lines;
	float cw = CharWidthApprox(fontSize);
	size_t maxChars = cw > 0.f ? sdl3::Max(1, int(maxWidth / cw)) : text.size() + 1;
	size_t start = 0, n = text.size();
	while (start < n) {
		size_t remaining = n - start;
		if (remaining <= maxChars) {
			lines.push_back({start, text.Substr(start)});
			break;
		}
		size_t breakAt = start + maxChars;
		size_t lastSpace = text.Rfind(' ', breakAt - 1);
		bool useSpace = lastSpace != String::npos && lastSpace >= start && lastSpace < breakAt;
		size_t cut = useSpace ? lastSpace : breakAt;
		lines.push_back({start, text.Substr(start, cut - start)});
		start = useSpace ? cut + 1 : cut; // saute l'espace de coupure
	}
	if (lines.empty())
		lines.push_back({0, String()});
	return lines;
}

/// Découpe `text` en lignes sur `\n` (UiInputArea : pas de word-wrap, chaque
/// ligne logique = une ligne affichée).
[[nodiscard]] inline std::vector<TextLine> SplitLines(const String &text) {
	std::vector<TextLine> lines;
	size_t start = 0;
	while (true) {
		size_t nl = text.Find('\n', start);
		if (nl == String::npos) {
			lines.push_back({start, text.Substr(start)});
			break;
		}
		lines.push_back({start, text.Substr(start, nl - start)});
		start = nl + 1;
	}
	return lines;
}

/// Trouve l'index de la ligne (dans `lines`, résultat de wrapText/splitLines)
/// qui contient le décalage `pos` — la dernière dont `offset <= pos`.
[[nodiscard]] inline size_t LineIndexOf(const std::vector<TextLine> &lines, size_t pos) noexcept {
	size_t li = 0;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (pos >= lines[i].offset)
			li = i;
		else
			break;
	}
	return li;
}

/// Recule d'un point de code UTF-8 depuis `pos` (jamais sous 0).
[[nodiscard]] inline size_t PrevCodepoint(const String &text, size_t pos) noexcept {
	if (pos == 0)
		return 0;
	size_t n = pos - 1;
	while (n > 0 && (uint8_t(text.c_str()[n]) & 0xC0) == 0x80)
		--n;
	return n;
}

/// Avance d'un point de code UTF-8 depuis `pos` (jamais au-delà de text.size()).
[[nodiscard]] inline size_t NextCodepoint(const String &text, size_t pos) noexcept {
	size_t n = text.size();
	if (pos >= n)
		return n;
	++pos;
	while (pos < n && (uint8_t(text.c_str()[pos]) & 0xC0) == 0x80)
		++pos;
	return pos;
}

/// Décalage (octets, borné à une frontière de point de code) sous la souris
/// en (mx, my), pour un widget dont le contenu tient dans `lines` — mêmes
/// marges (8px/4px) et défilement que le rendu (cf. RenderSystem). Utilisé
/// pour placer le curseur au clic et pendant un glisser de sélection.
[[nodiscard]] inline size_t HitTestOffset(const std::vector<TextLine> &lines, const String &text, float fontSize,
										  const sdl3::FRect &s, const sdl3::FPoint &scroll, float mx, float my) noexcept {
	if (lines.empty())
		return 0;
	float lh = LineHeightApprox(fontSize);
	float cw = CharWidthApprox(fontSize);
	float relY = my - (s.y + 4.f - scroll.y);
	int li = int(relY < 0.f ? 0.f : relY / lh);
	li = int(sdl3::Clamp(float(li), 0.f, float(lines.size() - 1)));
	const TextLine &line = lines[size_t(li)];
	float relX = mx - (s.x + 8.f - scroll.x);
	int col = cw > 0.f ? int(relX / cw + 0.5f) : 0;
	col = int(sdl3::Clamp(float(col), 0.f, float(line.text.size())));
	size_t off = line.offset + size_t(col);
	// Recale sur une frontière de point de code valide (col compte des
	// octets, pas des points de code — un caractère multi-octet pourrait
	// sinon retomber au milieu de sa propre séquence UTF-8).
	while (off > line.offset && off <= text.size() && (uint8_t(text.c_str()[off]) & 0xC0) == 0x80)
		--off;
	return off;
}

/// Ajuste r.scroll.y au minimum nécessaire pour que la ligne `lineIndex`
/// (dans les lignes affichées, enveloppées ou brutes) reste visible dans la
/// zone `c.screen` — utilisé pour garder le curseur de UiInput/UiInputArea
/// visible après un déplacement clavier/souris.
inline void ScrollIntoView(UiRect &r, const UiComputed &c, size_t lineIndex, float fontSize) noexcept {
	float lh = LineHeightApprox(fontSize);
	float lineTop = float(lineIndex) * lh;
	float viewH = sdl3::Max(1.f, c.screen.h - 8.f);
	if (lineTop < r.scroll.y)
		r.scroll.y = lineTop;
	else if (lineTop + lh > r.scroll.y + viewH)
		r.scroll.y = lineTop + lh - viewH;
	r.ClampScroll();
}

// ============================================================================
// LayoutSystem
// ============================================================================

class LayoutSystem {
public:
	/// Mesure du texte (largeur, hauteur). Par défaut : heuristique sans TTF ;
	/// brancher une vraie mesure via `measureText = ...` quand une Font existe.
	std::function<sdl3::FPoint(const String &, float fontSize)> measureText = [](const String &s, float fs) -> sdl3::FPoint {
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};

	/// Taille de police racine — base de DimUnit::Rem, et police héritée par
	/// les widgets sans fontSize propre (UiFactory l'aligne sur son thème).
	float rootFontSize = 14.f;

	void MarkDirty() noexcept { dirty = true; }
	[[nodiscard]] bool Dirty() const noexcept { return dirty; }
	[[nodiscard]] uint64_t PassCount() const noexcept { return passes; }

	/// Lance une passe seulement si nécessaire (dirty ou fenêtre redimensionnée).
	/// Retourne true si une passe a effectivement eu lieu.
	bool RunIfNeeded(ecs::ArchetypeRegistry &world, float screenW, float screenH) {
		if (!dirty && screenW == lastW && screenH == lastH)
			return false;
		Run(world, screenW, screenH);
		return true;
	}

	/// Passe complète inconditionnelle.
	void Run(ecs::ArchetypeRegistry &world, float screenW, float screenH) {
		++passes;
		lastW = screenW;
		lastH = screenH;
		dirty = false;

		Pass pass{*this, world, screenW, screenH};
		pass.Snapshot();

		sdl3::FRect full{0.f, 0.f, screenW, screenH};
		for (ecs::Entity root : pass.roots) {
			auto [w, h] = pass.ResolveSize(root, screenW, screenH);
			sdl3::FRect screen = pass.rects[root.id].ResolveIn({0.f, 0.f}, screenW, screenH);
			screen.w = w;
			screen.h = h;
			pass.Place(root, screen, full);
		}

		pass.WriteBack();
	}

private:
	bool dirty = true;
	float lastW = -1.f, lastH = -1.f;
	uint64_t passes = 0;

	// ── Une passe de layout sur des instantanés des composants ──────────────
	struct Pass {
		LayoutSystem &sys;
		ecs::ArchetypeRegistry &world;
		float rootW, rootH;

		std::unordered_map<uint32_t, UiRect> rects;
		std::unordered_map<uint32_t, UiFlow> flows;
		std::unordered_map<uint32_t, UiItem> items;
		std::unordered_map<uint32_t, std::vector<ecs::Entity>> children;
		std::unordered_set<uint32_t> hidden;
		std::vector<ecs::Entity> roots;

		std::unordered_map<uint32_t, sdl3::FPoint> measured;   // mémorisation measure()
		std::unordered_map<uint32_t, UiComputed> out;    // résultats
		std::unordered_map<uint32_t, sdl3::FPoint> newContent; // contenu auto des conteneurs
		std::unordered_map<uint32_t, sdl3::FPoint> newSizes;   // tailles résolues (scroll)
		std::unordered_map<uint32_t, EmBases> fonts;     // bases Em/Pem/Rem par entité

		Pass(LayoutSystem &s, ecs::ArchetypeRegistry &w, float rw, float rh) : sys(s), world(w), rootW(rw), rootH(rh) {}

		void Snapshot() {
			world.Query<UiRect>([&](ecs::Entity e, UiRect &r) { rects[e.id] = r; });
			world.Query<UiFlow>([&](ecs::Entity e, UiFlow &f) { flows[e.id] = f; });
			world.Query<UiItem>([&](ecs::Entity e, UiItem &i) { items[e.id] = i; });
			world.Query<UiChildren>([&](ecs::Entity e, UiChildren &c) { children[e.id] = c.list; });
			world.Query<UiHidden>([&](ecs::Entity e, UiHidden &) { hidden.insert(e.id); });
			world.Query<UiRect>([&](ecs::Entity e, UiRect &) {
				if (!world.HasComponent<UiParent>(e) && !hidden.contains(e.id))
					roots.push_back(e);
			});
			ComputeFonts();
		}

		/// Précalcule les bases Em/Pem/Rem ET l'échelle de contenu composée
		/// (cf. EmBases::scale — node-graph inner-zoom, Phase 0 du plan
		/// node-graph) : la police effective d'un widget est sa fontSize
		/// propre (DÉJÀ multipliée par l'échelle cumulée à ce niveau), sinon
		/// celle héritée du parent TELLE QUELLE (pas de second facteur —
		/// hériter recopie une valeur déjà à l'échelle, cf. le commentaire
		/// de Dimension::Resolve()) — DFS depuis les racines, hérite de
		/// rootFontSize/échelle 1.0 au sommet. Fait une fois par passe, les
		/// Dimension::em/pem/scale se résolvent ensuite en O(1).
		void ComputeFonts() {
			// "Propre" = taille/échelle explicitement posée via
			// .FontSize()/.Style(...) sur l'entité (UiStyle inline) —
			// indépendant de la cascade complète (UiComputedStyle), qui n'a
			// pas forcément encore tourné à ce point (StyleSystem::Resolve()
			// est optionnel/découplé du layout — cf. styles.hpp).
			std::unordered_map<uint32_t, float> ownFontSize;
			std::unordered_map<uint32_t, float> ownScale;
			world.Query<UiStyle>([&](ecs::Entity e, UiStyle &s) {
				if (const auto *fs = s.Get<prop::FontSize>())
					ownFontSize[e.id] = fs->value;
				if (const auto *iz = s.Get<prop::InnerZoom>())
					ownScale[e.id] = iz->value;
			});

			const float ROOT_FS = sys.rootFontSize;
			// (entité, police EFFECTIVE DÉJÀ mise à l'échelle du parent, échelle cumulée du parent)
			std::vector<std::tuple<ecs::Entity, float, float>> stack;
			for (ecs::Entity r : roots)
				stack.push_back({r, ROOT_FS, 1.f});
			while (!stack.empty()) {
				auto [e, parentFsScaled, parentScale] = stack.back();
				stack.pop_back();

				// Composition multiplicative (cf. Décision #3 du plan node-graph) :
				// un inner-zoom de 0.5 sous un inner-zoom de 2.0 ambiant donne 1.0.
				float scale = parentScale;
				if (auto it = ownScale.find(e.id); it != ownScale.end())
					scale = parentScale * it->second;

				float eff;
				if (auto it = ownFontSize.find(e.id); it != ownFontSize.end())
					eff = it->second * scale; // valeur d'auteur (design) mise à l'échelle ICI
				else
					eff = parentFsScaled; // héritage : copie la valeur du parent DÉJÀ à l'échelle

				fonts[e.id] = {eff, parentFsScaled, ROOT_FS, scale};
				if (auto it = children.find(e.id); it != children.end())
					for (ecs::Entity c : it->second)
						stack.push_back({c, eff, scale});
			}
		}

		[[nodiscard]] EmBases FontsOf(ecs::Entity e) const {
			auto it = fonts.find(e.id);
			return it != fonts.end() ? it->second : EmBases{sys.rootFontSize, sys.rootFontSize, sys.rootFontSize};
		}

		[[nodiscard]] UiItem ItemOf(ecs::Entity e) const {
			auto it = items.find(e.id);
			return it != items.end() ? it->second : UiItem{};
		}

		/// Spécifications de taille : UiItem si présent, sinon la taille px du
		/// UiRect — et Auto (taille intrinsèque) pour tout axe laissé à 0.
		[[nodiscard]] std::pair<Dimension, Dimension> SizeVals(ecs::Entity e) const {
			auto it = items.find(e.id);
			if (it != items.end())
				return {it->second.width, it->second.height};
			const auto &r = rects.at(e.id);
			return {r.size.x > 0.f ? Dimension::Px(r.size.x) : Dimension::Auto(),
					r.size.y > 0.f ? Dimension::Px(r.size.y) : Dimension::Auto()};
		}

		[[nodiscard]] std::vector<ecs::Entity> VisibleChildren(ecs::Entity e) const {
			std::vector<ecs::Entity> outv;
			auto it = children.find(e.id);
			if (it == children.end())
				return outv;
			for (ecs::Entity c : it->second)
				if (!hidden.contains(c.id) && rects.contains(c.id))
					outv.push_back(c);
			return outv;
		}

		/// Taille intrinsèque d'un widget feuille (sans UiFlow).
		[[nodiscard]] sdl3::FPoint Intrinsic(ecs::Entity e) {
			// Taille de police effective déjà résolue par computeFonts() (sa
			// propre valeur si posée via .FontSize()/.Style(...), sinon
			// héritée du parent) — même donnée que celle qui sert aux unités
			// Em/Rem, pas de nouvelle résolution ici.
			float fs = FontsOf(e).em;
			if (auto l = world.GetComponent<UiLabel>(e); l.IsSome())
				return sys.measureText(l.Unwrap()->text, fs);
			if (auto b = world.GetComponent<UiButton>(e); b.IsSome()) {
				auto t = sys.measureText(b.Unwrap()->text, fs);
				return {t.x + 24.f, t.y + 14.f};
			}
			if (world.HasComponent<UiToggle>(e))
				return {44.f, 24.f};
			if (world.HasComponent<UiCheckbox>(e))
				return {20.f, 20.f};
			if (auto s = world.GetComponent<UiSlider>(e); s.IsSome())
				return s.Unwrap()->orient == Orientation::Horizontal ? sdl3::FPoint{160.f, 20.f} : sdl3::FPoint{20.f, 160.f};
			if (world.HasComponent<UiProgress>(e))
				return {160.f, 8.f};
			if (auto sp = world.GetComponent<UiSeparator>(e); sp.IsSome())
				return sp.Unwrap()->orient == Orientation::Horizontal ? sdl3::FPoint{10.f, sp.Unwrap()->thickness}
																	  : sdl3::FPoint{sp.Unwrap()->thickness, 10.f};
			if (world.HasComponent<UiInput>(e))
				return {160.f, fs + 14.f};
			if (world.HasComponent<UiInputArea>(e))
				return {240.f, fs * 1.3f * 4.f + 14.f}; // ~4 lignes visibles
			if (world.HasComponent<UiDragValue>(e))
				return {90.f, fs + 14.f};
			if (world.HasComponent<UiColorSwatch>(e))
				return {28.f, 28.f};
			if (world.HasComponent<UiSVSquare>(e))
				return {160.f, 160.f};
			if (world.HasComponent<UiHueSlider>(e))
				return {160.f, 16.f};
			if (world.HasComponent<UiAlphaSlider>(e))
				return {160.f, 16.f};
			if (auto mb = world.GetComponent<UiMenuBarItem>(e); mb.IsSome()) {
				auto t = sys.measureText(mb.Unwrap()->text, fs);
				return {t.x + 20.f, t.y + 12.f};
			}
			if (auto mi = world.GetComponent<UiMenuItem>(e); mi.IsSome()) {
				auto t = sys.measureText(mi.Unwrap()->text, fs);
				float shortcutW =
					mi.Unwrap()->shortcut.IsEmpty() ? 0.f : sys.measureText(mi.Unwrap()->shortcut, fs).x + 20.f;
				float arrowW = mi.Unwrap()->hasSubmenu ? 16.f : 0.f;
				return {t.x + 24.f + shortcutW + arrowW, t.y + 12.f};
			}
			if (auto tb = world.GetComponent<UiTable>(e); tb.IsSome()) {
				const UiTable &t = *tb.Unwrap();
				return {t.TotalWidth(), t.headerHeight + t.ContentHeight()};
			}
			if (world.HasComponent<UiPlot>(e))
				return {300.f, 200.f};
			if (world.HasComponent<UiCalendar>(e))
				return {220.f, 200.f};
			if (auto rd = world.GetComponent<UiRadio>(e); rd.IsSome()) {
				auto t = sys.measureText(rd.Unwrap()->text, fs);
				return {18.f + (rd.Unwrap()->text.IsEmpty() ? 0.f : 6.f + t.x), sdl3::Max(18.f, t.y)};
			}
			if (auto sb = world.GetComponent<UiScrollBar>(e); sb.IsSome())
				return sb.Unwrap()->orient == Orientation::Horizontal ? sdl3::FPoint{160.f, 12.f} : sdl3::FPoint{12.f, 160.f};
			if (world.HasComponent<UiKnob>(e))
				return {48.f, 48.f};
			if (world.HasComponent<UiCanvas>(e))
				return {100.f, 100.f};
			if (world.HasComponent<UiComboBox>(e))
				return {160.f, fs + 14.f};
			if (world.HasComponent<ListBox>(e))
				return {160.f, 120.f};
			if (world.HasComponent<UiSpinner>(e))
				return {28.f, 28.f};
			if (auto bd = world.GetComponent<UiBadge>(e); bd.IsSome()) {
				auto t = sys.measureText(bd.Unwrap()->text, fs);
				return {t.x + 14.f, t.y + 4.f};
			}
			return rects.at(e.id).size;
		}

		/// Taille auto (rétrécir au contenu) sur un axe.
		float AutoSize(ecs::Entity e, bool isW) {
			auto fit = flows.find(e.id);
			if (fit == flows.end()) {
				auto s = Intrinsic(e);
				return isW ? s.x : s.y;
			}
			const UiFlow &flow = fit->second;
			// gap/margin/padding : mêmes float/math::Sides bruts qu'ailleurs, mis à
			// l'échelle explicitement avec le scale DE `e` (cf. place()/
			// placeLinear() plus bas — même raisonnement).
			const float SCALE = FontsOf(e).scale;
			auto kids = VisibleChildren(e);
			float main = 0.f, cross = 0.f;
			int n = 0;
			for (ecs::Entity c : kids) {
				auto it = ItemOf(c);
				if (it.attach != AttachLayout::RELATIVE)
					continue;
				++n;
				sdl3::FPoint cm = Measure(c);
				float cw = cm.x + it.margin.H() * SCALE, ch = cm.y + it.margin.V() * SCALE;
				if (flow.dir == LayoutDir::Column) {
					main += ch;
					cross = sdl3::Max(cross, cw);
				} else {
					main += cw;
					cross = sdl3::Max(cross, ch);
				}
			}
			if (n > 1)
				main += flow.gap * SCALE * float(n - 1);
			float w = (flow.dir == LayoutDir::Column) ? cross : main;
			float h = (flow.dir == LayoutDir::Column) ? main : cross;
			return isW ? (w + flow.padding.H() * SCALE) : (h + flow.padding.V() * SCALE);
		}

		/// Taille intrinsèque de `e` (post-ordre, mémoïsée).
		sdl3::FPoint Measure(ecs::Entity e) {
			if (auto it = measured.find(e.id); it != measured.end())
				return it->second;
			measured[e.id] = {0.f, 0.f}; // garde anti-cycle
			auto [wv, hv] = SizeVals(e);
			EmBases fb = FontsOf(e);
			float w = wv.IsAuto() ? AutoSize(e, true) : (wv.IsGrow() ? 0.f : wv.Resolve(0.f, rootW, fb));
			float h = hv.IsAuto() ? AutoSize(e, false) : (hv.IsGrow() ? 0.f : hv.Resolve(0.f, rootH, fb));
			auto it = ItemOf(e);
			sdl3::FPoint m{ClampOpt(w, it.minWidth, it.maxWidth), ClampOpt(h, it.minHeight, it.maxHeight)};
			measured[e.id] = m;
			return m;
		}

		/// Résout la taille pixel de `e` selon les dimensions de contenu du parent.
		///
		/// Grow résout ici à "remplit le parent" (parentW/parentH), pas à 0 :
		/// hors du repartitionnement pondéré multi-enfants de placeLinear() (qui
		/// écrase ki.main sur l'axe principal après coup, donc insensible à cette
		/// valeur), resolveSize() sert à placer un enfant seul face à une boîte
		/// déjà connue — racine dans run(), enfant absolu dans place(), et surtout
		/// l'axe TRANSVERSE d'un enfant en flow (ex: .GrowW() sur une Row dans une
		/// Column) : il n'y a alors aucun autre widget avec qui partager l'espace,
		/// donc Grow == tout l'espace disponible.
		std::pair<float, float> ResolveSize(ecs::Entity e, float parentW, float parentH) {
			auto [wv, hv] = SizeVals(e);
			EmBases fb = FontsOf(e);
			float w = wv.IsAuto() ? Measure(e).x : (wv.IsGrow() ? parentW : wv.Resolve(parentW, rootW, fb));
			float h = hv.IsAuto() ? Measure(e).y : (hv.IsGrow() ? parentH : hv.Resolve(parentH, rootH, fb));
			auto it = ItemOf(e);
			w = ClampOpt(w, it.minWidth, it.maxWidth);
			h = ClampOpt(h, it.minHeight, it.maxHeight);
			return {w, h};
		}

		void Place(ecs::Entity e, sdl3::FRect screen, sdl3::FRect drawClip) {
			UiRect &r = rects[e.id];
			r.size = {screen.w, screen.h};
			r.ClampScroll();

			UiComputed c;
			c.screen = screen;
			c.clip = drawClip;
			// Toujours borné à sa propre boîte : le contenu d'un widget ne doit
			// jamais s'afficher hors de sa zone absolue. `clipContent` ne
			// contrôle donc plus QUE le scroll (molette + auto-scrollbar) —
			// le clip, lui, est inconditionnel. Les enfants détachés
			// (AttachLayout::Fixed) contournent volontairement ce clip via
			// leur propre chemin dans place() (voir plus bas).
			c.childClip = drawClip.Intersection(screen);
			c.measured = measured.contains(e.id) ? measured[e.id] : sdl3::FPoint{screen.w, screen.h};

			auto fit = flows.find(e.id);
			const bool HAS_FLOW = fit != flows.end();
			// padding n'est PAS un Dimension (juste un math::Sides/math::Sides brut en px)
			// donc ne bénéficie pas automatiquement du scale appliqué dans
			// Dimension::Resolve() — mis à l'échelle explicitement ici, avec
			// le scale de CE conteneur (cf. EmBases::scale, Phase 0 node-graph).
			const float SELF_SCALE = FontsOf(e).scale;
			const math::Sides RAW_PAD = HAS_FLOW ? fit->second.padding : math::Sides{};
			const math::Sides PAD{RAW_PAD.left * SELF_SCALE, RAW_PAD.top * SELF_SCALE, RAW_PAD.right * SELF_SCALE,
							RAW_PAD.bottom * SELF_SCALE};
			sdl3::FRect contentBox{screen.x + PAD.left, screen.y + PAD.top, sdl3::Max(0.f, screen.w - PAD.H()),
							 sdl3::Max(0.f, screen.h - PAD.V())};
			c.contentOrigin = {contentBox.x - r.scroll.x, contentBox.y - r.scroll.y};
			out[e.id] = c;
			newSizes[e.id] = {screen.w, screen.h};

			auto kids = VisibleChildren(e);
			if (kids.empty())
				return;

			std::vector<ecs::Entity> flowKids, absKids;
			for (ecs::Entity k : kids)
				(HAS_FLOW && ItemOf(k).attach == AttachLayout::RELATIVE ? flowKids : absKids).push_back(k);

			if (HAS_FLOW && !flowKids.empty())
				PlaceLinear(e, fit->second, contentBox, c.contentOrigin, c.childClip, flowKids);

			for (ecs::Entity k : absKids) {
				auto [w, h] = ResolveSize(k, contentBox.w, contentBox.h);
				UiRect kr = rects[k.id];
				kr.size = {w, h};
				sdl3::FRect ks = kr.ResolveIn(c.contentOrigin, contentBox.w, contentBox.h);
				ks.w = w;
				ks.h = h;
				// Fixed : même position (ancre/offset relatifs au parent), mais
				// clip élargi à la fenêtre entière — RenderSystem le dessine hors
				// de l'arbre normal (overlay), donc son clip d'entrée ne doit pas
				// rester borné à celui, potentiellement étroit, de son parent.
				bool isFixed = ItemOf(k).attach == AttachLayout::FIXED;
				Place(k, ks, isFixed ? sdl3::FRect{0.f, 0.f, rootW, rootH} : c.childClip);
			}
		}

		void PlaceLinear(ecs::Entity parent, const UiFlow &flow, sdl3::FRect box, sdl3::FPoint origin, sdl3::FRect childClip,
						 const std::vector<ecs::Entity> &kids) {
			const bool IS_COL = flow.dir == LayoutDir::Column;
			const float MAIN_SIZE = IS_COL ? box.h : box.w;
			const float CROSS_SIZE = IS_COL ? box.w : box.h;
			// gap/margin ne sont PAS des Dimension (floats/math::Sides bruts en px)
			// donc ne bénéficient pas automatiquement du scale appliqué dans
			// Dimension::Resolve() — mis à l'échelle explicitement ici, avec
			// le scale DU CONTENEUR `parent` (cf. EmBases::scale).
			const float SCALE = FontsOf(parent).scale;
			const float GAP = flow.gap * SCALE;

			struct KidInfo {
				ecs::Entity e;
				float main, cross, mB, mA, cB, cA;
				float growW;
			};
			std::vector<KidInfo> infos;
			infos.reserve(kids.size());

			float fixedMain = 0.f, growTotal = 0.f;
			for (ecs::Entity k : kids) {
				auto it = ItemOf(k);
				auto [w, h] = ResolveSize(k, box.w, box.h);
				auto [wv, hv] = SizeVals(k);
				KidInfo ki{k,
						   IS_COL ? h : w,
						   IS_COL ? w : h,
						   (IS_COL ? it.margin.top : it.margin.left) * SCALE,
						   (IS_COL ? it.margin.bottom : it.margin.right) * SCALE,
						   (IS_COL ? it.margin.left : it.margin.top) * SCALE,
						   (IS_COL ? it.margin.right : it.margin.bottom) * SCALE,
						   0.f};
				Dimension mainV = IS_COL ? hv : wv;
				if (mainV.IsGrow()) {
					ki.growW = sdl3::Max(0.001f, mainV.value);
					growTotal += ki.growW;
				} else
					fixedMain += ki.main;
				fixedMain += ki.mB + ki.mA;
				infos.push_back(ki);
			}
			if (infos.size() > 1)
				fixedMain += GAP * float(infos.size() - 1);

			// Distribution de l'espace restant aux enfants Grow.
			float avail = sdl3::Max(0.f, MAIN_SIZE - fixedMain);
			for (auto &ki : infos) {
				if (ki.growW <= 0.f)
					continue;
				float v = avail * ki.growW / growTotal;
				auto it = ItemOf(ki.e);
				ki.main = IS_COL ? ClampOpt(v, it.minHeight, it.maxHeight) : ClampOpt(v, it.minWidth, it.maxWidth);
			}

			float used = (infos.size() > 1) ? GAP * float(infos.size() - 1) : 0.f;
			for (auto &ki : infos)
				used += ki.main + ki.mB + ki.mA;

			float cursor = 0.f, between = GAP;
			switch (flow.justify) {
			case Justify::Start:
				break;
			case Justify::Center:
				cursor = (MAIN_SIZE - used) * 0.5f;
				break;
			case Justify::End:
				cursor = MAIN_SIZE - used;
				break;
			case Justify::SpaceBetween:
				if (infos.size() > 1)
					between += (MAIN_SIZE - used) / float(infos.size() - 1);
				break;
			}

			float maxCrossExtent = 0.f;
			for (size_t i = 0; i < infos.size(); ++i) {
				auto &ki = infos[i];
				auto it = ItemOf(ki.e);
				auto [wv, hv] = SizeVals(ki.e);
				Dimension crossV = IS_COL ? wv : hv;

				CrossAlign align = it.alignSelf.IsSome() ? it.alignSelf.Unwrap() : flow.align;
				float cross = ki.cross;
				if (align == CrossAlign::Stretch && crossV.IsAuto()) {
					float stretched = sdl3::Max(0.f, CROSS_SIZE - ki.cB - ki.cA);
					cross = IS_COL ? ClampOpt(stretched, it.minWidth, it.maxWidth)
								  : ClampOpt(stretched, it.minHeight, it.maxHeight);
				}

				float crossPos = ki.cB;
				switch (align) {
				case CrossAlign::Start:
				case CrossAlign::Stretch:
					break;
				case CrossAlign::Center:
					crossPos = (CROSS_SIZE - cross) * 0.5f;
					break;
				case CrossAlign::End:
					crossPos = CROSS_SIZE - cross - ki.cA;
					break;
				}

				cursor += ki.mB;
				sdl3::FRect ks = IS_COL ? sdl3::FRect{origin.x + crossPos, origin.y + cursor, cross, ki.main}
								 : sdl3::FRect{origin.x + cursor, origin.y + crossPos, ki.main, cross};
				Place(ki.e, ks, childClip);

				cursor += ki.main + ki.mA;
				if (i + 1 < infos.size())
					cursor += between;
				maxCrossExtent = sdl3::Max(maxCrossExtent, crossPos + cross + ki.cA);
			}

			// Taille du contenu (pour le scroll) : étendue totale + padding
			// (mise à l'échelle, cf. `scale` en tête de fonction).
			sdl3::FPoint content = IS_COL ? sdl3::FPoint{maxCrossExtent + flow.padding.H() * SCALE, cursor + flow.padding.V() * SCALE}
								   : sdl3::FPoint{cursor + flow.padding.H() * SCALE, maxCrossExtent + flow.padding.V() * SCALE};
			newContent[parent.id] = content;
		}

		void WriteBack() {
			world.Query<UiComputed>([&](ecs::Entity e, UiComputed &c) {
				if (auto it = out.find(e.id); it != out.end()) {
					c = it->second;
					out.erase(it);
				}
			});
			// Entités visitées qui n'avaient pas encore de UiComputed.
			for (auto &[id, c] : out) {
				world.Query<UiRect>([&](ecs::Entity e, UiRect &) {
					if (e.id == id && !world.HasComponent<UiComputed>(e))
						world.AddComponent(e, c);
				});
			}
			world.Query<UiRect>([&](ecs::Entity e, UiRect &r) {
				if (auto it = newSizes.find(e.id); it != newSizes.end())
					r.size = it->second;
				if (auto it = newContent.find(e.id); it != newContent.end())
					r.content = it->second;
				r.ClampScroll();
			});
		}
	};
};

// ============================================================================
// InputSystem — consomme directement les sdl3::Event (pas de structure
// intermédiaire à la charge de l'appelant : agréger plusieurs évènements
// dans un seul struct par frame perdait/écrasait des positions de souris et
// pouvait faire cohabiter pressed+released dans le même passage, d'où des
// widgets "corrompus" lors d'un clic rapide). Deux points d'entrée :
//
//   handleEvent(world, ev, layout) — un évènement SDL discret (bouton,
//     mouvement, molette, texte, touche) : hit-test + interaction immédiats,
//     avec les coordonnées EXACTES de CET évènement.
//   tick(world, layout, dt) — mises à jour dépendantes du temps réel et
//     indépendantes des évènements (animation toggle/spinner, clignotement
//     du curseur de saisie, minuteur d'infobulle) : à appeler une fois par
//     frame, en dehors de la boucle de poll.
// ============================================================================

/// Ouvre un popup ancré (non modal, cf. UiFactory::Popup()) : l'affiche et
/// mémorise `trigger` (pour la fermeture au clic extérieur, cf.
/// InputSystem::dispatch — les modales passent par InputSystem::OpenModal
/// à la place, qui gère en plus la pile de blocage). Déclarée AVANT
/// InputSystem (et non juste après, comme initialement en Phase 1) : Phase 5
/// (MenuBar/Menu) l'appelle depuis `InputSystem::dispatch()` — une fonction
/// libre appelée depuis le CORPS d'une méthode définie inline dans une
/// classe doit être déclarée avant cette classe, le "complete-class context"
/// ne s'applique qu'aux membres de la classe elle-même, pas aux fonctions
/// libres déclarées plus loin dans le fichier.
inline void OpenPopup(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity popupRoot, ecs::Entity trigger) {
	if (auto ps = world.GetComponent<UiPopupState>(popupRoot); ps.IsSome()) {
		ps.Unwrap()->open = true;
		ps.Unwrap()->trigger = trigger;
	}
	if (world.HasComponent<UiHidden>(popupRoot))
		world.RemoveComponent<UiHidden>(popupRoot);
	layout.MarkDirty();
}

/// Ferme un popup ancré ouvert via `openPopup`.
inline void ClosePopup(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity popupRoot) {
	if (auto ps = world.GetComponent<UiPopupState>(popupRoot); ps.IsSome())
		ps.Unwrap()->open = false;
	if (!world.HasComponent<UiHidden>(popupRoot))
		world.AddComponent(popupRoot, UiHidden{});
	layout.MarkDirty();
}

/// Remonte les UiParent depuis `e` (lui-même inclus) jusqu'à trouver une
/// entité porteuse d'un UiPopupState — le popup CONTENANT logiquement `e`
/// (cf. UiFactory::Menu()/subMenu(), Phase 5 : les items de menu sont
/// parentés sous leur popup). ecs::Entity{} invalide si `e` n'est dans aucun popup.
[[nodiscard]] inline ecs::Entity NearestPopupAncestor(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	ecs::Entity cur = e;
	while (cur.Valid()) {
		if (world.HasComponent<UiPopupState>(cur))
			return cur;
		auto p = world.GetComponent<UiParent>(cur);
		if (p.IsNone())
			break;
		cur = p.Unwrap()->parent;
	}
	return ecs::Entity{};
}

/// Repositionne un popup Fixed RACINE (sans UiParent — son ancre/offset se
/// résolvent alors directement contre la fenêtre, cf. LayoutSystem::Run())
/// juste sous `triggerScreen`, aligné à gauche — utilisé à l'OUVERTURE d'un
/// menu déroulant (cf. UiFactory::Menu()) ; la position est figée tant que
/// le menu reste ouvert (comportement standard, pas de suivi en direct d'un
/// redimensionnement de fenêtre pendant l'interaction).
inline void PositionPopupBelow(ecs::ArchetypeRegistry &world, ecs::Entity popupRoot, const sdl3::FRect &triggerScreen) {
	if (auto r = world.GetComponent<UiRect>(popupRoot); r.IsSome()) {
		r.Unwrap()->anchor = Anchor::TopLeft;
		r.Unwrap()->offset = {triggerScreen.x, triggerScreen.y + triggerScreen.h};
	}
}

/// Comme positionPopupBelow, mais à DROITE de `triggerScreen` — sous-menus
/// (cf. UiFactory::subMenu()).
inline void PositionPopupRightOf(ecs::ArchetypeRegistry &world, ecs::Entity popupRoot, const sdl3::FRect &triggerScreen) {
	if (auto r = world.GetComponent<UiRect>(popupRoot); r.IsSome()) {
		r.Unwrap()->anchor = Anchor::TopLeft;
		r.Unwrap()->offset = {triggerScreen.x + triggerScreen.w, triggerScreen.y};
	}
}

/// Ferme toute la chaîne de popups contenant `clickedItem` (cf.
/// UiFactory::Menu()/subMenu()) : son popup direct, puis — si ce popup a été
/// ouvert par un item lui-même situé dans un autre popup (cas d'un
/// sous-menu) — celui-là aussi, et ainsi de suite jusqu'à la racine de la
/// chaîne. Appelé quand un UiMenuItem SANS sous-menu est cliqué (exécute
/// puis ferme tout, comme un clic de menu classique).
inline void CloseMenuChain(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity clickedItem) {
	ecs::Entity popup = NearestPopupAncestor(world, clickedItem);
	while (popup.Valid()) {
		ecs::Entity trigger{};
		if (auto ps = world.GetComponent<UiPopupState>(popup); ps.IsSome())
			trigger = ps.Unwrap()->trigger;
		ClosePopup(world, layout, popup);
		popup = trigger.Valid() ? NearestPopupAncestor(world, trigger) : ecs::Entity{};
	}
}

class InputSystem {
public:
	float wheelSpeed = 40.f;

	/// Infobulle courante (à passer à RenderSystem::Run pour l'afficher).
	struct Tooltip {
		bool visible = false;
		String text;
		sdl3::FPoint pos{};
	};
	Tooltip tooltip;
	float tooltipDelay = 0.6f; ///< secondes de survol avant affichage

	/// Ouvre une modale : l'affiche, la pousse en haut de la pile modale —
	/// tant qu'elle y reste, dispatch() bloque tout le reste de l'arbre (cf.
	/// hitOk) et Échap la referme. `modalRoot` doit porter UiPopupState
	/// (cf. UiFactory::Modal()).
	void OpenModal(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity modalRoot) {
		if (auto ps = world.GetComponent<UiPopupState>(modalRoot); ps.IsSome()) {
			ps.Unwrap()->open = true;
			ps.Unwrap()->modal = true;
		}
		if (world.HasComponent<UiHidden>(modalRoot))
			world.RemoveComponent<UiHidden>(modalRoot);
		modalStack.push_back(modalRoot);
		layout.MarkDirty();
	}

	/// Ferme la modale du sommet de la pile SI elle correspond à `modalRoot`
	/// (no-op sinon — évite de dépiler la mauvaise modale par erreur si les
	/// appels s'entremêlent).
	void CloseModal(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity modalRoot) {
		if (modalStack.empty() || modalStack.back() != modalRoot)
			return;
		modalStack.pop_back();
		if (auto ps = world.GetComponent<UiPopupState>(modalRoot); ps.IsSome())
			ps.Unwrap()->open = false;
		if (!world.HasComponent<UiHidden>(modalRoot))
			world.AddComponent(modalRoot, UiHidden{});
		layout.MarkDirty();
	}

	[[nodiscard]] bool HasOpenModal() const noexcept { return !modalStack.empty(); }

	/// Widget le plus en avant sous le pointeur au dernier évènement traité
	/// (cf. HitTestIndex), entité invalide si le pointeur ne survole aucun
	/// widget dessiné. C'est LUI (et lui seul, avec ses ancêtres) qui a le
	/// droit de réagir — les widgets restés derrière sont inertes.
	[[nodiscard]] ecs::Entity FrontMostWidget() const noexcept { return frontMost; }

	/// Vrai si le pointeur est capté par l'UI — un widget est dessiné sous
	/// lui. Permet à l'application de ne PAS traiter le clic de son côté
	/// (picking 3D d'un viewport, raccourcis de carte...) quand l'UI
	/// s'interpose.
	[[nodiscard]] bool PointerOverUi() const noexcept { return frontMost.Valid(); }

	/// Widget le plus en avant sous un point QUELCONQUE, en réutilisant
	/// l'index déjà tenu à jour par cette InputSystem — à préférer à
	/// ui::HitTestTopMost() dans tout code appelé à chaque évènement (celui-ci
	/// reconstruit l'index à chaque appel, cf. HitTestIndex).
	[[nodiscard]] ecs::Entity FrontMostAt(ecs::ArchetypeRegistry &world, LayoutSystem &layout, sdl3::FPoint p) {
		hitIndex.Refresh(world, layout.PassCount());
		return hitIndex.TopMostAt(world, p);
	}

	/// Index de hit-test de cette InputSystem — à invalider explicitement
	/// après un changement structurel qui ne passe ni par une passe de layout
	/// ni par une création/destruction d'entité (cf. HitTestIndex::Refresh).
	[[nodiscard]] HitTestIndex &HitIndex() noexcept { return hitIndex; }

	/// Traite UN évènement SDL. Ne fait rien pour les évènements sans effet
	/// sur l'UI (clavier hors backspace/entrée, fenêtre, joystick...).
	void HandleEvent(ecs::ArchetypeRegistry &world, const sdl3::Event &ev, LayoutSystem &layout) {
		Frame in;
		in.down = down;
		in.rightDown = rightDown;
		in.mouseX = mouseX;
		in.mouseY = mouseY;

		if (ev.IsMouseMotion()) {
			mouseX = in.mouseX = ev.MouseMotion().x;
			mouseY = in.mouseY = ev.MouseMotion().y;
		} else if (ev.IsMouseDown(SDL_BUTTON_LEFT)) {
			mouseX = in.mouseX = ev.MouseButton().x;
			mouseY = in.mouseY = ev.MouseButton().y;
			down = true;
			in.down = true;
			in.pressed = true;
			in.doubleClick = ev.MouseButton().clicks >= 2;
		} else if (ev.IsMouseUp(SDL_BUTTON_LEFT)) {
			mouseX = in.mouseX = ev.MouseButton().x;
			mouseY = in.mouseY = ev.MouseButton().y;
			down = false;
			in.down = false;
			in.released = true;
		} else if (ev.IsMouseDown(SDL_BUTTON_RIGHT)) {
			// Uniquement utilisé par UiPlot::boxZooming (zoom-rectangle,
			// Phase 3) pour l'instant — aucun autre widget ne lit
			// in.rightDown/rightPressed/rightReleased.
			mouseX = in.mouseX = ev.MouseButton().x;
			mouseY = in.mouseY = ev.MouseButton().y;
			rightDown = true;
			in.rightDown = true;
			in.rightPressed = true;
		} else if (ev.IsMouseUp(SDL_BUTTON_RIGHT)) {
			mouseX = in.mouseX = ev.MouseButton().x;
			mouseY = in.mouseY = ev.MouseButton().y;
			rightDown = false;
			in.rightDown = false;
			in.rightReleased = true;
		} else if (ev.IsMouseWheel()) {
			in.wheelY = ev.MouseWheel().y;
		} else if (ev.IsTextInput()) {
			in.textInput = String(ev.TextInput().text);
		} else if (ev.IsKeyDown()) {
			bool ctrl = (ev.raw.key.mod & SDL_KMOD_CTRL) != 0;
			in.shift = (ev.raw.key.mod & SDL_KMOD_SHIFT) != 0;
			if (ev.Keycode() == SDLK_BACKSPACE)
				in.backspace = true;
			else if (ev.Keycode() == SDLK_DELETE)
				in.del = true;
			else if (ev.Keycode() == SDLK_RETURN)
				in.enter = true;
			else if (ev.Keycode() == SDLK_LEFT)
				in.arrowLeft = true;
			else if (ev.Keycode() == SDLK_RIGHT)
				in.arrowRight = true;
			else if (ev.Keycode() == SDLK_UP)
				in.arrowUp = true;
			else if (ev.Keycode() == SDLK_DOWN)
				in.arrowDown = true;
			else if (ev.Keycode() == SDLK_HOME)
				in.home = true;
			else if (ev.Keycode() == SDLK_END)
				in.end = true;
			else if (ev.Keycode() == SDLK_ESCAPE)
				in.escape = true;
			else if (ctrl && ev.Keycode() == SDLK_C)
				in.copy = true;
			else if (ctrl && ev.Keycode() == SDLK_V)
				in.paste = true;
			else if (ctrl && ev.Keycode() == SDLK_X)
				in.cut = true;
			else
				return;
		} else {
			return;
		}

		Dispatch(world, in, layout);
	}

	/// Mises à jour continues indépendantes des évènements discrets : à
	/// appeler une fois par frame (animations, clignotement, infobulle).
	void Tick(ecs::ArchetypeRegistry &world, LayoutSystem &layout, float dt) {
		world.Query<UiToggle>([&](ecs::Entity, UiToggle &t) {
			float target = t.checked ? 1.f : 0.f;
			float speed = 10.f * dt;
			t.animT += sdl3::Clamp(target - t.animT, -speed, speed);
		});
		world.Query<UiSpinner>([&](ecs::Entity, UiSpinner &sp) { sp.angle = sdl3::Fmod(sp.angle + sp.speed * dt, 360.f); });
		world.Query<UiInput>([&](ecs::Entity, UiInput &f) {
			if (f.focused)
				f.blink += dt;
		});
		world.Query<UiInputArea>([&](ecs::Entity, UiInputArea &f) {
			if (f.focused)
				f.blink += dt;
		});
		world.Query<UiDragValue>([&](ecs::Entity, UiDragValue &d) {
			if (d.editing)
				d.blink += dt;
		});

		// ── UiInput : contenu = texte enveloppé (word-wrap) dans la largeur
		// du widget ; le défilement suit le CURSEUR (pas toujours la fin —
		// il peut être déplacé n'importe où dans le texte enveloppé).
		world.Query<UiInput, UiRect, UiComputed>([&](ecs::Entity e, UiInput &f, UiRect &r, UiComputed &c) {
			r.clipContent = true;
			float fs = GetResolved(world, e).FontSize(14.f);
			float innerW = sdl3::Max(1.f, c.screen.w - 16.f); // cf. marge de drawTextCentered (8px de chaque côté)
			auto lines = WrapText(f.text, fs, innerW);
			r.content = {c.screen.w, sdl3::Max(c.screen.h, float(lines.size()) * LineHeightApprox(fs) + 8.f)};
			ScrollIntoView(r, c, LineIndexOf(lines, f.cursor), fs);
		});

		// ── UiInputArea : contenu = lignes brutes (pas de wrap). Suit le
		// curseur pendant une interaction active (focus) ; sinon ne suit le
		// bas automatiquement que si l'utilisateur n'a pas remonté consulter
		// l'historique (ex. panneau de logs qui reçoit de nouvelles lignes).
		world.Query<UiInputArea, UiRect, UiComputed>([&](ecs::Entity e, UiInputArea &f, UiRect &r, UiComputed &c) {
			r.clipContent = true;
			float fs = GetResolved(world, e).FontSize(14.f);
			auto lines = SplitLines(f.text);
			float maxLineW = 0.f;
			for (const TextLine &l : lines)
				maxLineW = sdl3::Max(maxLineW, float(l.text.size()) * CharWidthApprox(fs));
			bool wasAtBottom = r.MaxScroll().y <= 0.f || r.scroll.y >= r.MaxScroll().y - 1.f;
			r.content = {maxLineW + 16.f, float(lines.size()) * LineHeightApprox(fs) + 8.f};
			r.ClampScroll();
			f.stickToBottom = wasAtBottom;
			if (f.focused)
				ScrollIntoView(r, c, LineIndexOf(lines, f.cursor), fs);
			else if (wasAtBottom)
				r.scroll.y = r.MaxScroll().y;
		});

		const float MX = mouseX, MY = mouseY;
		// Même filtre de premier plan que Dispatch : l'infobulle affichée est
		// celle du widget VU sous le curseur, pas celle d'un widget recouvert.
		UpdateFrontMost(world, layout, sdl3::FPoint{MX, MY});
		auto hitOk = [&](ecs::Entity e, const UiComputed &c) {
			// Test géométrique EN PREMIER : il rejette l'immense majorité des
			// widgets sans une seule recherche de composant (les trois tests
			// suivants remontent tous la chaîne des parents).
			sdl3::FPoint p{MX, MY};
			if (!c.screen.Contains(p) || !c.clip.Contains(p))
				return false;
			if (!IsFrontMost(e))
				return false;
			if (!modalStack.empty() && !IsDescendantOrSelf(world, e, modalStack.back()))
				return false;
			return !IsHiddenRecursive(world, e) && !IsDisabledRecursive(world, e);
		};
		ecs::Entity tipE{};
		float bestArea = 1e30f;
		String tipText;
		world.Query<UiTooltip, UiComputed>([&](ecs::Entity e, UiTooltip &t, UiComputed &c) {
			if (!hitOk(e, c))
				return;
			float area = c.screen.w * c.screen.h;
			if (area < bestArea) {
				bestArea = area;
				tipE = e;
				tipText = t.text;
			}
		});
		if (tipE.Valid() && tipE == lastTip)
			hoverTime += dt;
		else {
			hoverTime = 0.f;
			lastTip = tipE;
		}
		tooltip.visible = tipE.Valid() && hoverTime >= tooltipDelay && !down;
		tooltip.text = tipText;
		tooltip.pos = {MX + 14.f, MY + 20.f};
	}

private:
	float mouseX = 0.f, mouseY = 0.f;
	// Widget le plus en avant sous le pointeur, recalculé une fois par
	// évènement (Dispatch) et par frame (Tick) — cf. HitTestIndex et
	// IsFrontMost : c'est ce qui empêche deux widgets superposés de réagir
	// tous les deux au même survol/clic.
	ecs::Entity frontMost{};
	/// frontMost + ses ancêtres (cf. IsFrontMost) — vide quand le pointeur
	/// n'est au-dessus d'aucun widget, ce qui vaut "aucun blocage".
	std::vector<ecs::Entity> frontMostChain;
	/// Ordre de dessin mémorisé, reconstruit seulement quand la géométrie
	/// change (cf. HitTestIndex).
	HitTestIndex hitIndex;
	bool down = false;
	bool rightDown = false; ///< cf. Frame::rightDown (UiPlot::boxZooming, Phase 3)
	ecs::Entity lastTip{};
	float hoverTime = 0.f;

	// Drag en cours sur un pouce d'auto-scrollbar (un seul à la fois).
	ecs::Entity scrollDrag{};
	bool scrollDragVertical = true;
	float scrollDragStartMouse = 0.f;
	float scrollDragStartOffset = 0.f;

	// Glisser-souris en cours pour étendre une sélection de texte dans un
	// UiInput/UiInputArea (un seul à la fois, comme scrollDrag ci-dessus).
	ecs::Entity textDrag{};

	// Drag en cours sur une UiResizeHandle (un seul à la fois, cf.
	// interaction.hpp — splitters, colonnes de tableau).
	ecs::Entity resizeDrag{};

	// Drag en cours sur une UiReorderable (un seul à la fois, cf.
	// interaction.hpp — Selectable/TreeNode, Phase 4) — `reorderTarget` :
	// frère le plus proche du curseur au moment du relâchement (position
	// d'insertion), recalculé à chaque mouvement.
	ecs::Entity reorderDrag{};
	ecs::Entity reorderTarget{};

	// Drag en cours sur une UiDraggable (un seul à la fois, cf.
	// interaction.hpp — panneaux flottants du "bureau simulé", Phase 9/10).
	ecs::Entity draggableDrag{};

	// Pile des modales ouvertes (cf. openModal/closeModal, public plus bas) —
	// le sommet bloque tout le reste de l'arbre dans dispatch() (hitOk).
	std::vector<ecs::Entity> modalStack;

	/// Vrai si `e` a le droit de réagir au pointeur cette frame : c'est la
	/// cible de premier plan elle-même, ou l'un de ses ANCÊTRES. Les ancêtres
	/// restent servis parce qu'un widget composite n'est presque jamais la
	/// feuille dessinée en dernier : un UiButton est recouvert par son propre
	/// libellé, un conteneur scrollable par tout son contenu — les exclure
	/// rendrait la moitié des widgets inertes. En revanche un FRÈRE resté
	/// derrière ne passe plus : c'est exactement le blocage recherché.
	///
	/// Cible invalide (pointeur au-dessus du vide, ou entité hors de tout
	/// arbre dessiné — cas des mondes montés à la main dans les tests) :
	/// aucun blocage, on retombe sur le comportement historique du rect seul.
	[[nodiscard]] bool IsFrontMost(ecs::Entity e) const noexcept {
		if (frontMostChain.empty())
			return true;
		// Chaîne = la cible puis ses ancêtres (quelques entrées) : une
		// comparaison linéaire sur un petit vecteur contigu, là où un
		// IsDescendantOrSelf par widget remonterait les parents — donc ferait
		// une recherche de composant par niveau — pour CHACUNE des ~25 passes
		// Query<> de Dispatch.
		for (ecs::Entity a : frontMostChain)
			if (a == e)
				return true;
		return false;
	}

	/// Recalcule la cible de premier plan et sa chaîne d'ancêtres.
	void UpdateFrontMost(ecs::ArchetypeRegistry &world, LayoutSystem &layout, sdl3::FPoint p) {
		hitIndex.Refresh(world, layout.PassCount());
		frontMost = hitIndex.TopMostAt(world, p);
		frontMostChain.clear();
		for (ecs::Entity cur = frontMost; cur.Valid();) {
			frontMostChain.push_back(cur);
			auto parent = world.GetComponent<UiParent>(cur);
			if (parent.IsNone())
				break;
			cur = parent.Unwrap()->parent;
		}
	}

	/// État d'UN évènement à traiter — détail d'implémentation privé
	/// (remplace l'ancienne InputState publique que l'appelant devait
	/// construire lui-même par agrégation, source de corruption).
	struct Frame {
		float mouseX = 0.f, mouseY = 0.f;
		bool down = false;
		bool pressed = false;
		bool released = false;
		bool doubleClick = false; ///< pressed avec clicks>=2 (SDL) — cf. UiDragValue
		bool rightDown = false;   ///< bouton DROIT — cf. UiPlot::boxZooming (Phase 3), seul consommateur
		bool rightPressed = false;
		bool rightReleased = false;
		float wheelY = 0.f;
		String textInput;
		bool backspace = false;
		bool del = false; ///< touche Suppr (supprime après le curseur)
		bool enter = false;
		bool escape = false; ///< Échap : ferme la modale du sommet de la pile
		bool copy = false;   ///< Ctrl+C
		bool paste = false;  ///< Ctrl+V
		bool cut = false;    ///< Ctrl+X
		bool arrowLeft = false, arrowRight = false, arrowUp = false, arrowDown = false;
		bool home = false, end = false;
		bool shift = false; ///< Maj enfoncée (flèches/Origine/Fin → étend la sélection)
	};

	/// Déplacement du curseur (flèches/Origine/Fin), avec ou sans extension
	/// de sélection (Maj). `lines` : lignes actuellement affichées
	/// (enveloppées pour UiInput, brutes pour UiInputArea) — nécessaires pour
	/// Haut/Bas et Origine/Fin, qui opèrent sur la ligne AFFICHÉE courante
	/// (pas sur le texte entier). Sans Maj, une sélection existante se
	/// referme sur le bord gauche/droit (Gauche/Droite) plutôt que de
	/// bouger d'un cran depuis `cursor` — comportement standard.
	static void MoveCursor(const String &text, const std::vector<TextLine> &lines, size_t &cursor,
						   size_t &selectionAnchor, const Frame &in) {
		bool hadSelection = cursor != selectionAnchor;
		size_t lo = sdl3::Min(cursor, selectionAnchor), hi = sdl3::Max(cursor, selectionAnchor);
		size_t newCursor = cursor;

		if (in.arrowLeft) {
			newCursor = (!in.shift && hadSelection) ? lo : PrevCodepoint(text, cursor);
		} else if (in.arrowRight) {
			newCursor = (!in.shift && hadSelection) ? hi : NextCodepoint(text, cursor);
		} else if (in.arrowUp || in.arrowDown) {
			size_t li = LineIndexOf(lines, cursor);
			size_t col = cursor - lines[li].offset;
			size_t targetLi = in.arrowUp ? (li > 0 ? li - 1 : li) : sdl3::Min(li + 1, lines.size() - 1);
			newCursor = lines[targetLi].offset + sdl3::Min(col, lines[targetLi].text.size());
		} else if (in.home) {
			newCursor = lines[LineIndexOf(lines, cursor)].offset;
		} else if (in.end) {
			size_t li = LineIndexOf(lines, cursor);
			newCursor = lines[li].offset + lines[li].text.size();
		} else {
			return; // aucune touche de déplacement dans cet évènement
		}

		cursor = newCursor;
		if (!in.shift)
			selectionAnchor = newCursor;
	}

	/// Insertion/suppression/copie/collage/coupe à la position du CURSEUR
	/// (remplace toute sélection active), partagés entre UiInput et
	/// UiInputArea. `enterInsertsNewline` : true pour UiInputArea (Entrée =
	/// retour à la ligne) ; pour UiInput (Entrée = soumission), l'appelant
	/// traite `in.enter` lui-même et passe false ici.
	[[nodiscard]] static bool ApplyTextEdit(String &text, size_t &cursor, size_t &selectionAnchor, IOMode mode,
											size_t maxLen, const Frame &in, bool enterInsertsNewline) {
		bool changed = false;
		auto hasSelection = [&] { return cursor != selectionAnchor; };
		auto selLo = [&] { return sdl3::Min(cursor, selectionAnchor); };
		auto selHi = [&] { return sdl3::Max(cursor, selectionAnchor); };
		auto deleteSelection = [&] {
			size_t lo = selLo(), hi = selHi();
			String before = text.Substr(0, lo);
			before.Append(text.Substr(hi));
			text = std::move(before);
			cursor = selectionAnchor = lo;
		};
		auto insertAtCursor = [&](const String &s) {
			if (s.IsEmpty())
				return;
			size_t room = maxLen > text.size() ? maxLen - text.size() : 0;
			String ins = s.size() <= room ? s : s.Substr(0, room);
			if (ins.IsEmpty())
				return;
			String before = text.Substr(0, cursor);
			before.Append(ins);
			before.Append(text.Substr(cursor));
			text = std::move(before);
			cursor += ins.size();
			selectionAnchor = cursor;
			changed = true;
		};

		if (IoCanWrite(mode)) {
			if (!in.textInput.IsEmpty()) {
				if (hasSelection())
					deleteSelection();
				insertAtCursor(in.textInput);
			}
			if (in.backspace) {
				if (hasSelection()) {
					deleteSelection();
					changed = true;
				} else if (cursor > 0) {
					size_t p = PrevCodepoint(text, cursor);
					String before = text.Substr(0, p);
					before.Append(text.Substr(cursor));
					text = std::move(before);
					cursor = selectionAnchor = p;
					changed = true;
				}
			}
			if (in.del) {
				if (hasSelection()) {
					deleteSelection();
					changed = true;
				} else if (cursor < text.size()) {
					size_t nx = NextCodepoint(text, cursor);
					String before = text.Substr(0, cursor);
					before.Append(text.Substr(nx));
					text = std::move(before);
					changed = true;
				}
			}
			if (in.enter && enterInsertsNewline) {
				if (hasSelection())
					deleteSelection();
				insertAtCursor(String("\n"));
			}
		}
		// Copie : la sélection si elle existe, sinon tout le texte (préserve
		// le raccourci "copier tous les logs" quand rien n'est sélectionné).
		if (in.copy && IoCanCopy(mode))
			sdl3::clipboard::SetText(hasSelection() ? text.Substr(selLo(), selHi() - selLo()) : text);
		if (in.cut && IoCanWrite(mode) && IoCanCopy(mode) && hasSelection()) {
			sdl3::clipboard::SetText(text.Substr(selLo(), selHi() - selLo()));
			deleteSelection();
			changed = true;
		}
		if (in.paste && IoCanPaste(mode)) {
			if (hasSelection())
				deleteSelection();
			insertAtCursor(sdl3::clipboard::GetText());
		}
		return changed;
	}

	/// Hit-test + interaction pour tous les widgets, pour UN évènement.
	/// Les callbacks applicatifs (onClick/onChange/...) sont copiés dans
	/// `pending` et exécutés APRÈS toutes les queries : un callback peut
	/// muter l'ECS de façon structurelle (ex: changer de scène), ce qui
	/// réalloue le vecteur d'archétypes et invaliderait une itération en
	/// cours si on l'appelait directement depuis l'intérieur d'un Query().
	void Dispatch(ecs::ArchetypeRegistry &world, const Frame &in, LayoutSystem &layout) {
		const float MX = in.mouseX, MY = in.mouseY;
		std::vector<std::function<void()>> pending;

		// Échap ferme la modale du sommet de la pile (et seulement elle —
		// pas les popups non-modaux, qui se ferment au clic extérieur ci-
		// dessous). N'affecte rien d'autre cette frame.
		if (in.escape && !modalStack.empty()) {
			ecs::Entity top = modalStack.back();
			CloseModal(world, layout, top);
			return;
		}

		// Widget le plus en avant sous le pointeur, calculé UNE fois pour tout
		// l'évènement (cf. HitTestIndex) : lui seul — et ses ancêtres —
		// pourra réagir, quel que soit l'ordre des Query<> ci-dessous, qui
		// suit le type des widgets et non leur profondeur d'affichage.
		UpdateFrontMost(world, layout, sdl3::FPoint{MX, MY});

		// La souris doit être dans la partie VISIBLE du widget : son rect écran
		// ET la région de clip héritée des ancêtres. Les widgets désactivés
		// (UiDisabled, récursif) sont inertes. Une modale ouverte bloque tout
		// le reste de l'arbre (seuls elle-même et ses descendants passent), et
		// un widget recouvert par un autre ne reçoit plus rien (IsFrontMost).
		auto hitOk = [&](ecs::Entity e, const UiComputed &c) {
			// Test géométrique EN PREMIER : il rejette l'immense majorité des
			// widgets sans une seule recherche de composant (les trois tests
			// suivants remontent tous la chaîne des parents).
			sdl3::FPoint p{MX, MY};
			if (!c.screen.Contains(p) || !c.clip.Contains(p))
				return false;
			if (!IsFrontMost(e))
				return false;
			if (!modalStack.empty() && !IsDescendantOrSelf(world, e, modalStack.back()))
				return false;
			return !IsHiddenRecursive(world, e) && !IsDisabledRecursive(world, e);
		};

		// Un combo ouvert capte le clic (sélection OU fermeture) : rien
		// d'autre ne doit le recevoir. Même logique pour la molette au-dessus
		// d'un widget qui la consomme (listbox, slider, knob).
		bool clickConsumed = false;
		bool wheelConsumed = false;

		// ── Popups ancrés (non-modaux) : ferme au clic en dehors du popup ET
		// de son déclencheur (un clic SUR le déclencheur est géré par le
		// widget lui-même, ex: re-cliquer un bouton qui rouvre). Les
		// popups sont collectés puis fermés hors itération (cf. le même
		// motif que StyleSystem::Resolve — retirer un composant PENDANT un
		// Query<T> sur ce même T invaliderait l'itération).
		if (in.pressed) {
			std::vector<ecs::Entity> toClose;
			world.Query<UiPopupState, UiComputed>([&](ecs::Entity e, UiPopupState &ps, UiComputed &c) {
				if (!ps.open || ps.modal)
					return;
				sdl3::FPoint p{MX, MY};
				bool insidePopup = c.screen.Contains(p);
				bool insideTrigger = false;
				if (ps.trigger.Valid()) {
					if (auto tc = world.GetComponent<UiComputed>(ps.trigger); tc.IsSome())
						insideTrigger = tc.Unwrap()->screen.Contains(p);
				}
				if (!insidePopup && !insideTrigger)
					toClose.push_back(e);
			});
			for (ecs::Entity e : toClose) {
				if (auto ps = world.GetComponent<UiPopupState>(e); ps.IsSome())
					ps.Unwrap()->open = false;
				if (!world.HasComponent<UiHidden>(e))
					world.AddComponent(e, UiHidden{});
			}
			if (!toClose.empty())
				layout.MarkDirty();
		}

		// ── ComboBox (en premier : la liste ouverte est un overlay modal) ───
		world.Query<UiComboBox, UiComputed>([&](ecs::Entity e, UiComboBox &cb, UiComputed &c) {
			cb.hovered = hitOk(e, c);
			cb.hoveredItem = -1;
			if (cb.open) {
				sdl3::FRect dd = cb.DropdownRect(c.screen);
				sdl3::FPoint p{MX, MY};
				// La liste est en overlay : pas de clip d'ancêtres à respecter.
				bool inDrop = dd.Contains(p) && IsFrontMost(e) && !IsHiddenRecursive(world, e) &&
							  !IsDisabledRecursive(world, e);
				if (inDrop && cb.itemHeight > 0.f)
					cb.hoveredItem = sdl3::Clamp(int((MY - dd.y) / cb.itemHeight), 0, int(cb.items.size()) - 1);
				if (in.wheelY != 0.f && inDrop)
					wheelConsumed = true;
				if (in.pressed && !clickConsumed) {
					clickConsumed = true;
					if (inDrop && cb.hoveredItem >= 0 && cb.hoveredItem != cb.selected) {
						cb.selected = cb.hoveredItem;
						if (auto cbk = world.GetComponent<UiCallbacks>(e); cbk.IsSome() && cbk.Unwrap()->onChange) {
							auto fn = cbk.Unwrap()->onChange;
							float v = float(cb.selected);
							pending.push_back([fn, v] { fn(v); });
						}
					}
					cb.open = false;
				}
			} else if (in.pressed && !clickConsumed && cb.hovered) {
				cb.open = true;
				clickConsumed = true;
			}
		});

		// ── Auto-scrollbar : drag du pouce ──────────────────────────────────
		// Un pouce en cours de drag capte la souris tant que le bouton reste
		// enfoncé, même si elle sort du rect du pouce (comportement standard
		// "drag" — cf. sliders/knobs plus bas, même logique).
		if (scrollDrag.Valid()) {
			if (in.down) {
				auto r = world.GetComponent<UiRect>(scrollDrag);
				auto c = world.GetComponent<UiComputed>(scrollDrag);
				if (r.IsSome() && c.IsSome()) {
					UiRect &rr = *r.Unwrap();
					float trackSize = scrollDragVertical ? c.Unwrap()->screen.h : c.Unwrap()->screen.w;
					float contentSize = scrollDragVertical ? rr.ContentSize().y : rr.ContentSize().x;
					float maxSc = scrollDragVertical ? rr.MaxScroll().y : rr.MaxScroll().x;
					float thumbSize =
						sdl3::Clamp(trackSize * trackSize / sdl3::Max(1.f, contentSize), K_SCROLLBAR_MIN_THUMB, trackSize);
					float draggable = sdl3::Max(1.f, trackSize - thumbSize);
					float delta = (scrollDragVertical ? MY : MX) - scrollDragStartMouse;
					float newOffset = sdl3::Clamp(scrollDragStartOffset + delta * (maxSc / draggable), 0.f, maxSc);
					if (scrollDragVertical)
						rr.scroll.y = newOffset;
					else
						rr.scroll.x = newOffset;
					layout.MarkDirty();
				}
				clickConsumed = true;
			}
			if (in.released)
				scrollDrag = ecs::Entity{};
		} else if (in.pressed && !clickConsumed) {
			ecs::Entity best{};
			bool bestVertical = true;
			float bestArea = 1e30f;
			sdl3::FPoint p{MX, MY};
			world.Query<UiRect, UiComputed>([&](ecs::Entity e, UiRect &r, UiComputed &c) {
				// Auto-scrollbar : réactif dès qu'il y a débordement réel
				// (r.maxScroll() > 0), sans opt-in explicite requis — cf.
				// vScrollbarThumbRect/hScrollbarThumbRect, qui renvoient déjà
				// un rect vide en l'absence de débordement.
				if (!IsFrontMost(e) || IsHiddenRecursive(world, e) || IsDisabledRecursive(world, e))
					return;
				float area = c.screen.w * c.screen.h;
				if (area >= bestArea)
					return;
				if (sdl3::FRect vt = VScrollbarThumbRect(r, c.screen); vt.w > 0.f && vt.Contains(p)) {
					bestArea = area;
					best = e;
					bestVertical = true;
				} else if (sdl3::FRect ht = HScrollbarThumbRect(r, c.screen); ht.w > 0.f && ht.Contains(p)) {
					bestArea = area;
					best = e;
					bestVertical = false;
				}
			});
			if (best.Valid()) {
				scrollDrag = best;
				scrollDragVertical = bestVertical;
				scrollDragStartMouse = bestVertical ? MY : MX;
				if (auto r = world.GetComponent<UiRect>(best); r.IsSome())
					scrollDragStartOffset = bestVertical ? r.Unwrap()->scroll.y : r.Unwrap()->scroll.x;
				clickConsumed = true;
			}
		}

		// ── UiResizeHandle : drag générique (splitters, colonnes de tableau,
		// cf. interaction.hpp) — même motif latch-puis-suit que l'auto-
		// scrollbar ci-dessus.
		if (resizeDrag.Valid()) {
			if (in.down) {
				if (auto h = world.GetComponent<UiResizeHandle>(resizeDrag); h.IsSome()) {
					float coord = h.Unwrap()->orient == Orientation::Horizontal ? MX : MY;
					ResolveResizeDrag(*h.Unwrap(), coord);
					layout.MarkDirty();
				}
				clickConsumed = true;
			}
			if (in.released) {
				if (auto h = world.GetComponent<UiResizeHandle>(resizeDrag); h.IsSome())
					h.Unwrap()->dragging = false;
				resizeDrag = ecs::Entity{};
			}
		} else if (in.pressed && !clickConsumed) {
			world.Query<UiResizeHandle, UiComputed>([&](ecs::Entity e, UiResizeHandle &h, UiComputed &c) {
				if (resizeDrag.Valid() || clickConsumed || !hitOk(e, c))
					return;
				resizeDrag = e;
				h.dragging = true;
				h.dragStartMouse = h.orient == Orientation::Horizontal ? MX : MY;
				h.dragStartBefore = h.getBefore ? h.getBefore() : 0.f;
				h.dragStartAfter = h.getAfter ? h.getAfter() : 0.f;
				clickConsumed = true;
			});
		} else if (!resizeDrag.Valid()) {
			// Survol (hors drag) : utile pour un futur curseur redimension.
			world.Query<UiResizeHandle, UiComputed>(
				[&](ecs::Entity e, UiResizeHandle &h, UiComputed &c) { h.hovered = hitOk(e, c); });
		}

		// ── UiDraggable : déplacement/redimensionnement de panneau flottant
		// (Phase 9/10 : cf. interaction.hpp, ui::PanelChrome) — même motif
		// latch-puis-suit que UiResizeHandle ci-dessus.
		if (draggableDrag.Valid()) {
			if (in.down) {
				if (auto d = world.GetComponent<UiDraggable>(draggableDrag); d.IsSome()) {
					ecs::Entity target = d.Unwrap()->target.Valid() ? d.Unwrap()->target : draggableDrag;
					sdl3::FPoint delta{MX - d.Unwrap()->dragStartMouse.x, MY - d.Unwrap()->dragStartMouse.y};
					if (d.Unwrap()->resizeMode) {
						if (auto it = world.GetComponent<UiItem>(target); it.IsSome()) {
							float nw = sdl3::Max(d.Unwrap()->minWidth, d.Unwrap()->dragStartSize.x + delta.x);
							float nh = sdl3::Max(d.Unwrap()->minHeight, d.Unwrap()->dragStartSize.y + delta.y);
							it.Unwrap()->width = Dimension::Px(nw);
							it.Unwrap()->height = Dimension::Px(nh);
						}
					} else if (auto r = world.GetComponent<UiRect>(target); r.IsSome()) {
						r.Unwrap()->offset = {d.Unwrap()->dragStartOffset.x + delta.x,
											  d.Unwrap()->dragStartOffset.y + delta.y};
					}
					layout.MarkDirty();
				}
				clickConsumed = true;
			}
			if (in.released) {
				if (auto d = world.GetComponent<UiDraggable>(draggableDrag); d.IsSome())
					d.Unwrap()->dragging = false;
				draggableDrag = ecs::Entity{};
			}
		} else if (in.pressed && !clickConsumed) {
			world.Query<UiDraggable, UiComputed>([&](ecs::Entity e, UiDraggable &d, UiComputed &c) {
				if (draggableDrag.Valid() || clickConsumed || !hitOk(e, c))
					return;
				draggableDrag = e;
				d.dragging = true;
				d.dragStartMouse = {MX, MY};
				ecs::Entity target = d.target.Valid() ? d.target : e;
				if (auto r = world.GetComponent<UiRect>(target); r.IsSome())
					d.dragStartOffset = r.Unwrap()->offset;
				if (auto it = world.GetComponent<UiItem>(target); it.IsSome())
					d.dragStartSize = {it.Unwrap()->width.value, it.Unwrap()->height.value};
				clickConsumed = true;
			});
		}

		// Clic effectif pour tous les widgets suivants.
		const bool PRESSED = in.pressed && !clickConsumed;

		// ── Boutons ─────────────────────────────────────────────────────────
		world.Query<UiButton, UiComputed>([&](ecs::Entity e, UiButton &b, UiComputed &c) {
			b.clicked = false;
			bool hover = hitOk(e, c);
			b.hovered = hover;
			if (PRESSED && hover)
				b.pressed = true;
			if (in.released) {
				if (b.pressed && hover) {
					b.clicked = true;
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onClick) {
						auto fn = cb.Unwrap()->onClick;
						pending.push_back(std::move(fn));
					}
				}
				b.pressed = false;
			}
		});

		// ── Toggle / Checkbox ────────────────────────────────────────────────
		world.Query<UiToggle, UiComputed>([&](ecs::Entity e, UiToggle &t, UiComputed &c) {
			t.hovered = hitOk(e, c);
			if (PRESSED && t.hovered) {
				t.checked = !t.checked;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onToggle) {
					auto fn = cb.Unwrap()->onToggle;
					bool v = t.checked;
					pending.push_back([fn, v] { fn(v); });
				}
			}
		});
		world.Query<UiCheckbox, UiComputed>([&](ecs::Entity e, UiCheckbox &cb, UiComputed &c) {
			cb.hovered = hitOk(e, c);
			if (PRESSED && cb.hovered) {
				cb.checked = !cb.checked;
				if (auto callback = world.GetComponent<UiCallbacks>(e); callback.IsSome() && callback.Unwrap()->onToggle) {
					auto fn = callback.Unwrap()->onToggle;
					bool v = cb.checked;
					pending.push_back([fn, v] { fn(v); });
				}
			}
		});

		// ── Sliders (drag + molette, quantifiés par `step`) ──────────────────
		world.Query<UiSlider, UiComputed>([&](ecs::Entity e, UiSlider &s, UiComputed &c) {
			s.hovered = hitOk(e, c);
			if (PRESSED && s.hovered)
				s.dragging = true;
			if (in.released)
				s.dragging = false;
			float nv = s.value;
			if (s.dragging && in.down) {
				float t = (s.orient == Orientation::Horizontal) ? (MX - c.screen.x) / sdl3::Max(1.f, c.screen.w)
																: 1.f - (MY - c.screen.y) / sdl3::Max(1.f, c.screen.h);
				nv = s.Snap(s.min + sdl3::Clamp(t, 0.f, 1.f) * (s.max - s.min));
			} else if (s.hovered && in.wheelY != 0.f && !wheelConsumed) {
				float delta = s.step > 0.f ? s.step : (s.max - s.min) * 0.02f;
				nv = s.Snap(s.value + in.wheelY * delta);
				wheelConsumed = true;
			}
			if (nv != s.value) {
				s.value = nv;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
					auto fn = cb.Unwrap()->onChange;
					float v = s.value;
					pending.push_back([fn, v] { fn(v); });
				}
			}
		});

		// ── Radios : exclusivité par groupe ──────────────────────────────────
		// 1re passe : détecter le clic ; 2de passe : décocher le reste du groupe
		// (pas de changement structurel → itérations sûres ; callbacks différés
		// quand même car un onToggle utilisateur pourrait, lui, muter l'ECS).
		{
			ecs::Entity clicked{};
			String clickedGroup;
			world.Query<UiRadio, UiComputed>([&](ecs::Entity e, UiRadio &r, UiComputed &c) {
				r.hovered = hitOk(e, c);
				if (PRESSED && r.hovered && !r.checked) {
					clicked = e;
					clickedGroup = r.group;
				}
			});
			if (clicked.Valid()) {
				world.Query<UiRadio>([&](ecs::Entity e, UiRadio &r) {
					if (r.group != clickedGroup)
						return;
					bool nowChecked = (e == clicked);
					if (r.checked == nowChecked)
						return;
					r.checked = nowChecked;
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onToggle) {
						auto fn = cb.Unwrap()->onToggle;
						pending.push_back([fn, nowChecked] { fn(nowChecked); });
					}
				});
			}
		}

		// ── ScrollBars autonomes ─────────────────────────────────────────────
		world.Query<UiScrollBar, UiComputed>([&](ecs::Entity e, UiScrollBar &sb, UiComputed &c) {
			sb.hovered = hitOk(e, c);
			if (PRESSED && sb.hovered)
				sb.dragging = true;
			if (in.released)
				sb.dragging = false;
			if (sb.dragging && in.down && sb.MaxOffset() > 0.f) {
				bool horiz = sb.orient == Orientation::Horizontal;
				float trackLen = horiz ? c.screen.w : c.screen.h;
				float thumbLen = trackLen * sb.ThumbRatio();
				float pos = horiz ? (MX - c.screen.x) : (MY - c.screen.y);
				// Le centre du pouce suit la souris.
				float t = (pos - thumbLen * 0.5f) / sdl3::Max(1.f, trackLen - thumbLen);
				float nv = sdl3::Clamp(t, 0.f, 1.f) * sb.MaxOffset();
				if (nv != sb.offset) {
					sb.offset = nv;
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onScroll) {
						auto fn = cb.Unwrap()->onScroll;
						float v = sb.offset;
						pending.push_back([fn, v] { fn(v); });
					}
				}
			}
		});

		// ── Knobs : drag vertical (vers le haut = augmente) + molette ────────
		world.Query<UiKnob, UiComputed>([&](ecs::Entity e, UiKnob &k, UiComputed &c) {
			k.hovered = hitOk(e, c);
			if (PRESSED && k.hovered) {
				k.dragging = true;
				k.dragStartY = MY;
				k.dragStartValue = k.value;
			}
			if (in.released)
				k.dragging = false;
			float nv = k.value;
			if (k.dragging && in.down) {
				// 200 px de drag couvrent toute la plage.
				nv = k.Snap(k.dragStartValue + (k.dragStartY - MY) * 0.005f * (k.max - k.min));
			} else if (k.hovered && in.wheelY != 0.f && !wheelConsumed) {
				float delta = k.step > 0.f ? k.step : (k.max - k.min) * 0.02f;
				nv = k.Snap(k.value + in.wheelY * delta);
				wheelConsumed = true;
			}
			if (nv != k.value) {
				k.value = nv;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
					auto fn = cb.Unwrap()->onChange;
					float v = k.value;
					pending.push_back([fn, v] { fn(v); });
				}
			}
		});

		// ── DragValue : glisser horizontal change la valeur (comme un slider
		// sans piste visible) ; un DOUBLE-clic bascule en édition texte
		// directe (cf. UiDragValue — saisie minimale, curseur toujours en fin
		// de texte). Un simple clic sans glisser ne fait rien (contrairement
		// à un bouton) : c'est le double-clic, seul, qui ouvre l'édition.
		world.Query<UiDragValue, UiComputed>([&](ecs::Entity e, UiDragValue &d, UiComputed &c) {
			d.hovered = hitOk(e, c);

			if (d.editing) {
				if (in.escape) {
					d.editing = false;
					return;
				}
				bool commit = in.enter || (PRESSED && !d.hovered);
				if (commit) {
					float nv = d.Snap(d.editText.ToFloat());
					d.editing = false;
					if (nv != d.value) {
						d.value = nv;
						if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
							auto fn = cb.Unwrap()->onChange;
							float v = d.value;
							pending.push_back([fn, v] { fn(v); });
						}
					}
					return;
				}
				if (in.backspace && !d.editText.IsEmpty())
					d.editText = d.editText.Substr(0, d.editText.size() - 1);
				for (char ch : in.textInput) {
					bool isSign = ch == '-' && d.editText.IsEmpty();
					bool isDigit = ch >= '0' && ch <= '9';
					bool isDot = ch == '.' && !d.editText.Contains('.');
					if (isDigit || isSign || isDot)
						d.editText.push_back(ch);
				}
				return;
			}

			if (PRESSED && d.hovered) {
				d.dragging = true;
				d.dragMoved = false;
				d.dragWasDoubleClick = in.doubleClick;
				d.dragStartX = MX;
				d.dragStartValue = d.value;
			} else if (d.dragging && in.down) {
				float dx = MX - d.dragStartX;
				if (sdl3::Abs(dx) > 2.f)
					d.dragMoved = true;
				float nv = d.Snap(d.dragStartValue + dx * d.speed);
				if (nv != d.value) {
					d.value = nv;
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
						auto fn = cb.Unwrap()->onChange;
						float v = d.value;
						pending.push_back([fn, v] { fn(v); });
					}
				}
			} else if (in.released && d.dragging) {
				bool wantEdit = d.hovered && d.dragWasDoubleClick;
				d.dragging = false;
				if (wantEdit) {
					d.editing = true;
					d.editText = d.Formatted();
					d.blink = 0.f;
				}
			} else if (d.hovered && in.wheelY != 0.f && !wheelConsumed) {
				float delta = d.step > 0.f ? d.step : (d.max - d.min) * 0.02f;
				float nv = d.Snap(d.value + in.wheelY * delta);
				if (nv != d.value) {
					d.value = nv;
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
						auto fn = cb.Unwrap()->onChange;
						float v = d.value;
						pending.push_back([fn, v] { fn(v); });
					}
				}
				wheelConsumed = true;
			}
		});

		// ── ColorSwatch : interaction identique à un bouton (hovered/pressed
		// /clicked → onClick), sans texte — cf. UiFactory::ColorSwatch.
		world.Query<UiColorSwatch, UiComputed>([&](ecs::Entity e, UiColorSwatch &sw, UiComputed &c) {
			sw.clicked = false;
			bool hover = hitOk(e, c);
			sw.hovered = hover;
			if (PRESSED && hover)
				sw.pressed = true;
			if (in.released) {
				if (sw.pressed && hover) {
					sw.clicked = true;
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onClick) {
						auto fn = cb.Unwrap()->onClick;
						pending.push_back(std::move(fn));
					}
				}
				sw.pressed = false;
			}
		});

		// ── UiColorPicker : carré SV (drag 2D) + glissières teinte/alpha
		// (drag 1D) — cf. UiFactory::ColorPicker. Chacune notifie son
		// UiCallbacks::onChange (valeur ignorée, sert de simple "ping" : le
		// composite relit l'état courant des 3 widgets pour se resynchroniser
		// — cf. factory.hpp).
		world.Query<UiSVSquare, UiComputed>([&](ecs::Entity e, UiSVSquare &sv, UiComputed &c) {
			sv.hovered = hitOk(e, c);
			if (PRESSED && sv.hovered)
				sv.dragging = true;
			if (in.released)
				sv.dragging = false;
			if (sv.dragging && in.down) {
				float ns = sdl3::Clamp((MX - c.screen.x) / sdl3::Max(1.f, c.screen.w), 0.f, 1.f);
				float nv = sdl3::Clamp(1.f - (MY - c.screen.y) / sdl3::Max(1.f, c.screen.h), 0.f, 1.f);
				if (ns != sv.s || nv != sv.v) {
					sv.s = ns;
					sv.v = nv;
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
						auto fn = cb.Unwrap()->onChange;
						pending.push_back([fn] { fn(0.f); });
					}
				}
			}
		});
		world.Query<UiHueSlider, UiComputed>([&](ecs::Entity e, UiHueSlider &h, UiComputed &c) {
			h.hovered = hitOk(e, c);
			if (PRESSED && h.hovered)
				h.dragging = true;
			if (in.released)
				h.dragging = false;
			if (h.dragging && in.down) {
				float t = sdl3::Clamp((MX - c.screen.x) / sdl3::Max(1.f, c.screen.w), 0.f, 1.f);
				float nh = t * 360.f;
				if (nh != h.hue) {
					h.hue = nh;
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
						auto fn = cb.Unwrap()->onChange;
						pending.push_back([fn] { fn(0.f); });
					}
				}
			}
		});
		world.Query<UiAlphaSlider, UiComputed>([&](ecs::Entity e, UiAlphaSlider &a, UiComputed &c) {
			a.hovered = hitOk(e, c);
			if (PRESSED && a.hovered)
				a.dragging = true;
			if (in.released)
				a.dragging = false;
			if (a.dragging && in.down) {
				float t = sdl3::Clamp((MX - c.screen.x) / sdl3::Max(1.f, c.screen.w), 0.f, 1.f);
				if (t != a.alpha) {
					a.alpha = t;
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
						auto fn = cb.Unwrap()->onChange;
						pending.push_back([fn] { fn(0.f); });
					}
				}
			}
		});

		// ── Selectable/TreeNode : clic applique applySelectionClick (ctrl/
		// shift lus via l'état clavier COURANT — SDL_MouseButtonEvent ne
		// porte pas les modificateurs, contrairement aux évènements clavier)
		// sur le UiSelection du plus proche ANCÊTRE qui en porte un. 2 passes
		// comme les UiRadio d'un groupe plus haut : 1re détecte le clic (un
		// clic sur la FLÈCHE d'un UiTreeNode replie/déplie à la place, cf.
		// bloc suivant — n'affecte pas la sélection) et calcule le nouvel
		// état ; 2de reflète `.selected` sur tout le conteneur.
		{
			bool ctrlDown = (sdl3::keyboard::Mods() & SDL_KMOD_CTRL) != 0;
			ecs::Entity clickedContainer{};
			world.Query<UiSelectable, UiComputed>([&](ecs::Entity e, UiSelectable &sel, UiComputed &c) {
				sel.hovered = hitOk(e, c);
				if (PRESSED && sel.hovered) {
					ecs::Entity container = NearestSelectionAncestor(world, e);
					if (container.Valid()) {
						if (auto us = world.GetComponent<UiSelection>(container); us.IsSome()) {
							ApplySelectionClick(us.Unwrap()->state, sel.index, ctrlDown, in.shift,
												us.Unwrap()->multiSelect);
							clickedContainer = container;
						}
					}
				}
			});
			world.Query<UiTreeNode, UiComputed>([&](ecs::Entity e, UiTreeNode &tn, UiComputed &c) {
				sdl3::FPoint p{MX, MY};
				sdl3::FRect header{c.screen.x, c.screen.y, c.screen.w, tn.headerHeight};
				sdl3::FRect arrow{c.screen.x, c.screen.y, 20.f, tn.headerHeight};
				bool overHeader = header.Contains(p) && c.clip.Contains(p) && IsFrontMost(e) &&
								  !IsHiddenRecursive(world, e) &&
								  !IsDisabledRecursive(world, e);
				tn.hovered = overHeader;
				tn.hoveredArrow = overHeader && arrow.Contains(p);
				if (PRESSED && overHeader && !tn.hoveredArrow) {
					ecs::Entity container = NearestSelectionAncestor(world, e);
					if (container.Valid()) {
						if (auto us = world.GetComponent<UiSelection>(container); us.IsSome()) {
							ApplySelectionClick(us.Unwrap()->state, tn.index, ctrlDown, in.shift,
												us.Unwrap()->multiSelect);
							clickedContainer = container;
						}
					}
				}
			});
			if (clickedContainer.Valid()) {
				if (auto us = world.GetComponent<UiSelection>(clickedContainer); us.IsSome()) {
					const auto &selectedSet = us.Unwrap()->state.selected;
					world.Query<UiSelectable>([&](ecs::Entity e, UiSelectable &sel) {
						if (NearestSelectionAncestor(world, e) == clickedContainer)
							sel.selected = selectedSet.contains(sel.index);
					});
					world.Query<UiTreeNode>([&](ecs::Entity e, UiTreeNode &tn) {
						if (NearestSelectionAncestor(world, e) == clickedContainer)
							tn.selected = selectedSet.contains(tn.index);
					});
				}
			}
		}

		// ── UiTreeNode : clic sur la FLÈCHE replie/déplie les enfants
		// (structurel → différé, même motif que UiExpander).
		{
			std::vector<ecs::Entity> toggled;
			world.Query<UiTreeNode, UiComputed>([&](ecs::Entity e, UiTreeNode &tn, UiComputed &) {
				if (PRESSED && tn.hoveredArrow) {
					tn.expanded = !tn.expanded;
					toggled.push_back(e);
				}
			});
			for (ecs::Entity e : toggled) {
				auto tn = world.GetComponent<UiTreeNode>(e);
				if (tn.IsNone())
					continue;
				bool expanded = tn.Unwrap()->expanded;
				if (auto ch = world.GetComponent<UiChildren>(e); ch.IsSome()) {
					std::vector<ecs::Entity> kids = ch.Unwrap()->list;
					for (ecs::Entity k : kids) {
						if (expanded)
							world.RemoveComponent<UiHidden>(k);
						else if (!world.HasComponent<UiHidden>(k))
							world.AddComponent(k, UiHidden{});
					}
				}
				layout.MarkDirty();
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onToggle) {
					auto fn = cb.Unwrap()->onToggle;
					pending.push_back([fn, expanded] { fn(expanded); });
				}
			}
		}

		// ── UiReorderable : glisser vertical réordonne l'entité parmi ses
		// frères directs (cf. interaction.hpp) — un seuil de mouvement
		// (dragMoved) évite qu'un simple clic de sélection (souvent posé sur
		// la MÊME entité, cf. blocs ci-dessus) ne déclenche un
		// réordonnancement parasite. Mute directement UiChildren du parent
		// (reorderChild) : l'ordre visuel EST la vérité.
		if (reorderDrag.Valid()) {
			if (in.down) {
				if (auto rc = world.GetComponent<UiReorderable>(reorderDrag); rc.IsSome()) {
					if (sdl3::Abs(MY - rc.Unwrap()->dragStartMouseY) > 4.f)
						rc.Unwrap()->dragMoved = true;
					if (rc.Unwrap()->dragMoved) {
						ecs::Entity parent{};
						if (auto p = world.GetComponent<UiParent>(reorderDrag); p.IsSome())
							parent = p.Unwrap()->parent;
						if (auto ch = world.GetComponent<UiChildren>(parent); ch.IsSome()) {
							ecs::Entity closest{};
							float bestDist = 1e30f;
							for (ecs::Entity sib : ch.Unwrap()->list) {
								if (sib == reorderDrag || !world.HasComponent<UiReorderable>(sib))
									continue;
								auto c = world.GetComponent<UiComputed>(sib);
								if (c.IsNone())
									continue;
								float cy = c.Unwrap()->screen.y + c.Unwrap()->screen.h * 0.5f;
								float d = sdl3::Abs(cy - MY);
								if (d < bestDist) {
									bestDist = d;
									closest = sib;
								}
							}
							if (closest.Valid())
								reorderTarget = closest;
						}
					}
				}
				clickConsumed = true;
			}
			if (in.released) {
				bool didMove = false;
				if (auto rc = world.GetComponent<UiReorderable>(reorderDrag); rc.IsSome()) {
					didMove = rc.Unwrap()->dragMoved;
					rc.Unwrap()->dragging = false;
					rc.Unwrap()->dragMoved = false;
				}
				if (didMove && reorderTarget.Valid() && reorderTarget != reorderDrag) {
					ecs::Entity parent{};
					if (auto p = world.GetComponent<UiParent>(reorderDrag); p.IsSome())
						parent = p.Unwrap()->parent;
					if (parent.Valid()) {
						int fromIdx = -1, toIdx = -1;
						if (auto ch = world.GetComponent<UiChildren>(parent); ch.IsSome()) {
							const auto &list = ch.Unwrap()->list;
							for (size_t i = 0; i < list.size(); ++i) {
								if (list[i] == reorderDrag)
									fromIdx = int(i);
								if (list[i] == reorderTarget)
									toIdx = int(i);
							}
						}
						ecs::Entity dragged = reorderDrag, target = reorderTarget;
						if (fromIdx >= 0 && toIdx >= 0 && fromIdx != toIdx) {
							ReorderChild(world, parent, dragged, target);
							layout.MarkDirty();
							if (auto cb = world.GetComponent<UiCallbacks>(parent);
								cb.IsSome() && cb.Unwrap()->onReorder) {
								auto fn = cb.Unwrap()->onReorder;
								pending.push_back([fn, fromIdx, toIdx] { fn(fromIdx, toIdx); });
							}
						}
					}
				}
				reorderDrag = ecs::Entity{};
				reorderTarget = ecs::Entity{};
			}
		} else if (PRESSED) {
			world.Query<UiReorderable, UiComputed>([&](ecs::Entity e, UiReorderable &rc, UiComputed &c) {
				if (reorderDrag.Valid() || !hitOk(e, c))
					return;
				reorderDrag = e;
				reorderTarget = ecs::Entity{};
				rc.dragging = true;
				rc.dragMoved = false;
				rc.dragStartMouseY = MY;
			});
		}

		// ── MenuBar / Menu / sous-menu (Phase 5) : clic sur un UiMenuBarItem
		// bascule son popup déroulant (positionné SOUS l'item à l'ouverture,
		// cf. positionPopupBelow) ; clic sur un UiMenuItem SANS sous-menu
		// exécute onClick puis ferme toute la chaîne de popups (cf.
		// closeMenuChain) ; AVEC sous-menu, bascule l'ouverture de celui-ci
		// (positionné à DROITE, cf. positionPopupRightOf) au lieu d'exécuter/
		// fermer. La fermeture au clic EXTÉRIEUR (tous niveaux) est déjà
		// gérée par le bloc popups non-modaux plus haut dans dispatch() —
		// chaque UiPopupState ouvert y est testé indépendamment, donc une
		// chaîne à plusieurs niveaux se referme déjà en cascade sans code
		// supplémentaire ici. openPopup/closePopup mutent UiHidden
		// (add_component/remove_component) : comme partout ailleurs dans
		// cette fonction, on COLLECTE d'abord (pendant les Query<>), on
		// applique APRÈS — muter pendant l'itération migrerait l'archétype
		// et pourrait réallouer le vecteur en cours de parcours.
		struct MenuToggle {
			ecs::Entity popup, trigger;
			sdl3::FRect triggerScreen;
			bool open;
		};
		std::vector<MenuToggle> menuToggles;
		std::vector<ecs::Entity> clickedLeaf;

		world.Query<UiMenuBarItem, UiComputed>([&](ecs::Entity e, UiMenuBarItem &mb, UiComputed &c) {
			mb.hovered = hitOk(e, c);
			if (PRESSED && mb.hovered && mb.menuPopup.Valid()) {
				bool open = false;
				if (auto ps = world.GetComponent<UiPopupState>(mb.menuPopup); ps.IsSome())
					open = ps.Unwrap()->open;
				menuToggles.push_back({mb.menuPopup, e, c.screen, !open});
			}
		});
		world.Query<UiMenuItem, UiComputed>([&](ecs::Entity e, UiMenuItem &mi, UiComputed &c) {
			mi.hovered = hitOk(e, c);
			if (PRESSED && mi.hovered) {
				if (mi.hasSubmenu && mi.submenuPopup.Valid()) {
					bool open = false;
					if (auto ps = world.GetComponent<UiPopupState>(mi.submenuPopup); ps.IsSome())
						open = ps.Unwrap()->open;
					menuToggles.push_back({mi.submenuPopup, e, c.screen, !open});
				} else {
					clickedLeaf.push_back(e);
				}
			}
		});
		for (auto &t : menuToggles) {
			if (t.open) {
				// Un item SANS sous-menu place le popup SOUS lui (barre de
				// menu) ; AVEC déclencheur = un UiMenuItem (donc DANS un
				// popup existant), à DROITE (sous-menu) — distingué via le
				// type du composant porté par `t.trigger`.
				if (world.HasComponent<UiMenuItem>(t.trigger))
					PositionPopupRightOf(world, t.popup, t.triggerScreen);
				else
					PositionPopupBelow(world, t.popup, t.triggerScreen);
				OpenPopup(world, layout, t.popup, t.trigger);
			} else {
				ClosePopup(world, layout, t.popup);
			}
		}
		for (ecs::Entity e : clickedLeaf) {
			if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onClick) {
				auto fn = cb.Unwrap()->onClick;
				pending.push_back(std::move(fn));
			}
			CloseMenuChain(world, layout, e);
		}

		// ── Table (Phase 6) : clic d'en-tête trie (bascule NONE→Asc→Desc→
		// NONE) SAUF dans la zone de redimensionnement (bordure droite de
		// colonne, ~6px) qui redimensionne à la place ; clic de ligne
		// applique applySelectionClick sur le UiSelection posé sur la MÊME
		// entité (cf. UiFactory::Table()) ; glisser une ligne (si
		// `reorderable`) la réordonne parmi ses sœurs — mêmes algorithmes
		// que UiResizeHandle/UiReorderable (Phase 2/4), réimplémentés ici
		// sur les tableaux internes de UiTable plutôt que sur des
		// accesseurs/entités indépendantes (cf. le composant, components.hpp,
		// pour la justification : une grille de cellules-entités serait
		// hors de propos pour un widget sans interactivité par cellule).
		world.Query<UiTable, UiComputed>([&](ecs::Entity e, UiTable &t, UiComputed &c) {
			bool ctrlDown = (sdl3::keyboard::Mods() & SDL_KMOD_CTRL) != 0;
			sdl3::FPoint p{MX, MY};
			bool overWidget = c.screen.Contains(p) && c.clip.Contains(p) && IsFrontMost(e) &&
							  !IsHiddenRecursive(world, e) && !IsDisabledRecursive(world, e);
			sdl3::FRect headerRect{c.screen.x, c.screen.y, c.screen.w, t.headerHeight};
			sdl3::FRect bodyRect{c.screen.x, c.screen.y + t.headerHeight, c.screen.w,
						   sdl3::Max(0.f, c.screen.h - t.headerHeight)};
			bool overHeader = overWidget && headerRect.Contains(p);
			bool overBody = overWidget && bodyRect.Contains(p);

			// Redimensionnement de colonne en cours : capte tout, rien
			// d'autre ne doit réagir tant que le bouton reste enfoncé.
			if (t.resizingColumn >= 0) {
				if (in.down) {
					float delta = MX - t.resizeStartMouseX;
					auto &col = t.columns[size_t(t.resizingColumn)];
					col.width = sdl3::Max(col.minWidth, t.resizeStartWidth + delta);
				}
				if (in.released)
					t.resizingColumn = -1;
				return;
			}

			// Glisser de ligne en cours : idem.
			if (t.reorderRow >= 0) {
				if (in.down) {
					if (sdl3::Abs(MY - t.reorderStartMouseY) > 4.f)
						t.reorderMoved = true;
					if (t.reorderMoved && overBody && !t.rows.empty()) {
						float relY = MY - bodyRect.y + t.scroll;
						int target = int(relY / t.rowHeight);
						t.reorderTargetRow = sdl3::Clamp(target, 0, int(t.rows.size()) - 1);
					}
				}
				if (in.released) {
					if (t.reorderMoved && t.reorderTargetRow >= 0 && t.reorderTargetRow != t.reorderRow &&
						t.reorderTargetRow < int(t.rows.size())) {
						int from = t.reorderRow, toIdx = t.reorderTargetRow;
						auto moved = std::move(t.rows[size_t(from)]);
						t.rows.erase(t.rows.begin() + from);
						t.rows.insert(t.rows.begin() + toIdx, std::move(moved));
						if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onReorder) {
							auto fn = cb.Unwrap()->onReorder;
							pending.push_back([fn, from, toIdx] { fn(from, toIdx); });
						}
					}
					t.reorderRow = -1;
					t.reorderMoved = false;
					t.reorderTargetRow = -1;
				}
				return;
			}

			// Survol (hors drag).
			t.hoveredColumn = -1;
			t.hoveredRow = -1;
			if (overHeader) {
				float relX = MX - c.screen.x;
				float x = 0.f;
				for (int i = 0; i < int(t.columns.size()); ++i) {
					float w = t.columns[size_t(i)].width;
					if (relX >= x && relX < x + w) {
						t.hoveredColumn = i;
						break;
					}
					x += w;
				}
			} else if (overBody) {
				float relY = MY - bodyRect.y + t.scroll;
				int row = int(relY / t.rowHeight);
				if (row >= 0 && row < int(t.rows.size()))
					t.hoveredRow = row;
			}

			if (overWidget && in.wheelY != 0.f && !wheelConsumed) {
				t.scroll = sdl3::Clamp(t.scroll - in.wheelY * wheelSpeed, 0.f, t.MaxScroll(bodyRect.h));
				wheelConsumed = true;
			}

			if (!PRESSED || !overWidget)
				return;

			if (overHeader && t.hoveredColumn >= 0) {
				// Zone de redimensionnement : les ~6 derniers pixels de la colonne.
				float edgeX = t.ColumnX(t.hoveredColumn) + t.columns[size_t(t.hoveredColumn)].width;
				float relX = MX - c.screen.x;
				if (relX >= edgeX - 6.f && relX <= edgeX + 6.f) {
					t.resizingColumn = t.hoveredColumn;
					t.resizeStartMouseX = MX;
					t.resizeStartWidth = t.columns[size_t(t.hoveredColumn)].width;
				} else if (t.columns[size_t(t.hoveredColumn)].sortable) {
					if (t.sortColumn != t.hoveredColumn) {
						t.sortColumn = t.hoveredColumn;
						t.sortOrder = SortOrder::ASCENDING;
					} else {
						t.sortOrder = t.sortOrder == SortOrder::ASCENDING    ? SortOrder::DESCENDING
									  : t.sortOrder == SortOrder::DESCENDING ? SortOrder::NONE
																			 : SortOrder::ASCENDING;
					}
					int col = t.sortColumn;
					if (t.sortOrder == SortOrder::NONE) {
						t.sortColumn = -1;
					} else {
						bool asc = t.sortOrder == SortOrder::ASCENDING;
						std::stable_sort(t.rows.begin(), t.rows.end(),
										 [col, asc](const std::vector<String> &a, const std::vector<String> &b) {
											 String av = size_t(col) < a.size() ? a[size_t(col)] : String();
											 String bv = size_t(col) < b.size() ? b[size_t(col)] : String();
											 return asc ? av < bv : bv < av;
										 });
					}
					// Le tri réordonne les lignes : les index de sélection ne
					// correspondraient plus aux bonnes lignes — plus sûr de
					// vider que d'afficher une surbrillance sur la mauvaise
					// ligne (limitation connue : pas d'ID de ligne stable).
					if (auto us = world.GetComponent<UiSelection>(e); us.IsSome())
						us.Unwrap()->state = SelectionState{};
					if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
						auto fn = cb.Unwrap()->onChange;
						float v = float(col);
						pending.push_back([fn, v] { fn(v); });
					}
				}
			} else if (overBody && t.hoveredRow >= 0) {
				if (auto us = world.GetComponent<UiSelection>(e); us.IsSome())
					ApplySelectionClick(us.Unwrap()->state, t.hoveredRow, ctrlDown, in.shift, us.Unwrap()->multiSelect);
				if (t.reorderable) {
					t.reorderRow = t.hoveredRow;
					t.reorderMoved = false;
					t.reorderStartMouseY = MY;
					t.reorderTargetRow = t.hoveredRow;
				}
			}
		});

		// ── Plot (cf. ui/plot.hpp) : survol (point le plus proche, pour
		// l'infobulle) + légende (survol = surbrillance, clic = bascule
		// visible/invisible d'une série) + navigation (Phase 3 : pan glisser
		// gauche, zoom molette par axe/combiné, zoom-rectangle glisser
		// droit, retour à l'ajustement automatique au double-clic).
		world.Query<UiPlot, UiComputed>([&](ecs::Entity e, UiPlot &p, UiComputed &c) {
			p.hovered = hitOk(e, c);
			sdl3::FRect plotRect = ComputePlotRect(p, c.screen);

			// ── PlotMode::PIE (Phase 5) : ni axes ni navigation (pan/zoom
			// n'ont pas de sens sur un camembert) — juste légende (survol/
			// bascule, comme XY) et survol direct d'une part.
			if (p.mode == PlotMode::PIE) {
				p.hoveredIndex = -1; // pas de notion d'"index dans la serie" pour Pie
				p.legendHover = -1;
				if (!p.hovered) {
					p.hoveredSeries = -1;
					return;
				}
				if (p.showLegend) {
					sdl3::FRect legendBox = LegendBoxRect(p, c.screen, plotRect);
					for (size_t i = 0; i < p.pieSlices.size(); ++i) {
						if (LegendRowRect(legendBox, i).Contains({MX, MY})) {
							p.legendHover = int(i);
							if (in.pressed)
								p.pieSlices[i].visible = !p.pieSlices[i].visible;
							break;
						}
					}
				}
				if (p.legendHover >= 0) {
					p.hoveredSeries = -1;
					return;
				}
				sdl3::FRect circleRect = ComputePieCircleRect(plotRect);
				sdl3::FPoint center{circleRect.x + circleRect.w * 0.5f, circleRect.y + circleRect.h * 0.5f};
				auto angles = ComputePieAngles(p.pieSlices);
				p.hoveredSeries = PieHitTest(p.pieSlices, angles, center, circleRect.w * 0.5f, {MX, MY});
				return;
			}

			// ── PlotMode::HEATMAP (Phase 6) : pas d'axes/légende/navigation —
			// juste le survol d'une cellule (valeur sous le curseur).
			if (p.mode == PlotMode::HEATMAP) {
				p.hoveredIndex = -1;
				p.legendHover = -1;
				p.hoveredSeries = p.hovered ? HeatmapHitTest(p.heatmap, plotRect, {MX, MY}) : -1;
				return;
			}

			// ── PlotMode::CANDLE (Phase 6) : graduations/grille réutilisées de
			// XY (cf. resolveCandleAxis()), mais PAS de pan/zoom/box-zoom/reset
			// — un chandelier n'a pas de plage manuelle à préserver d'une frame
			// à l'autre (toujours entièrement auto-ajusté), et PAS de légende
			// (une seule série de bougies par plot). Survol = bougie la plus
			// proche EN X (cf. candleNearestBar(), pas plotNearestPoint()).
			if (p.mode == PlotMode::CANDLE) {
				p.legendHover = -1;
				p.hoveredIndex = -1;
				if (!p.hovered) {
					p.hoveredSeries = -1;
					return;
				}
				PlotAxis xr = ResolveCandleAxis(p.xAxis, p.candleBars, true);
				p.hoveredSeries = CandleNearestBar(p.candleBars, plotRect, xr, {MX, MY});
				return;
			}

			// Poursuite d'un pan/zoom-rectangle déjà en cours : NE DÉPEND PAS
			// de `p.hovered` — l'utilisateur peut glisser hors du widget en
			// cours de geste (SDL ne clippe pas le mouvement au widget
			// d'origine), le geste doit continuer à suivre la souris.
			if (p.panning) {
				p.dragCurrentMouse = {MX, MY};
				if (in.down)
					ApplyPlotPan(p, plotRect, p.dragStartMouse, p.dragCurrentMouse, p.dragStartXAxis,
								p.dragStartYAxis, p.dragStartYAxis2);
				else
					p.panning = false;
				return;
			}
			if (p.boxZooming) {
				p.dragCurrentMouse = {MX, MY};
				if (!in.rightDown) {
					p.boxZooming = false;
					float dw = sdl3::Abs(p.dragCurrentMouse.x - p.dragStartMouse.x);
					float dh = sdl3::Abs(p.dragCurrentMouse.y - p.dragStartMouse.y);
					if (dw > 4.f && dh > 4.f) // vrai glisser, pas un simple clic-droit
						ApplyPlotBoxZoom(p, plotRect, p.dragStartMouse, p.dragCurrentMouse, p.dragStartXAxis,
										p.dragStartYAxis, p.dragStartYAxis2);
				}
				return;
			}

			p.hoveredSeries = p.hoveredIndex = -1;
			p.legendHover = -1;
			if (!p.hovered)
				return;

			if (p.showLegend) {
				sdl3::FRect legendBox = LegendBoxRect(p, c.screen, plotRect);
				for (size_t i = 0; i < p.series.size(); ++i) {
					if (LegendRowRect(legendBox, i).Contains({MX, MY})) {
						p.legendHover = int(i);
						if (in.pressed)
							p.series[i].visible = !p.series[i].visible;
						break;
					}
				}
			}
			if (p.legendHover >= 0)
				return; // un clic/survol sur la légende n'affecte pas le survol de données/la navigation

			if (in.pressed && in.doubleClick) {
				ResetPlotToAutoFit(p);
				return;
			}
			if (in.pressed) { // début d'un pan
				p.panning = true;
				p.hoveredSeries = p.hoveredIndex = -1;
				p.dragStartMouse = p.dragCurrentMouse = {MX, MY};
				p.dragStartXAxis = ResolveAxis(p.xAxis, p.series, true);
				p.dragStartYAxis = ResolveAxis(p.yAxis, p.series, false);
				p.dragStartYAxis2 = ResolveAxis(p.yAxis2, p.series, false, true);
				return;
			}
			if (in.rightPressed) { // début d'un zoom-rectangle
				p.boxZooming = true;
				p.hoveredSeries = p.hoveredIndex = -1;
				p.dragStartMouse = p.dragCurrentMouse = {MX, MY};
				p.dragStartXAxis = ResolveAxis(p.xAxis, p.series, true);
				p.dragStartYAxis = ResolveAxis(p.yAxis, p.series, false);
				p.dragStartYAxis2 = ResolveAxis(p.yAxis2, p.series, false, true);
				return;
			}
			if (in.wheelY != 0.f && !wheelConsumed) {
				// Molette au-dessus d'une bande de graduation RÉSERVÉE ->
				// zoome CET axe seul (primaire, secondaire — Phase 4 — ou X) ;
				// ailleurs sur le widget -> X + les DEUX axes Y ensemble (cf.
				// plan : "par axe ou combiné").
				bool overXTicks = !p.xAxis.tickOverlay &&
								  ((p.xAxis.tickPosition == PlotEdge::Bottom && MY > plotRect.y + plotRect.h) ||
								   (p.xAxis.tickPosition == PlotEdge::Top && MY < plotRect.y));
				bool overYTicks = !p.yAxis.tickOverlay &&
								  ((p.yAxis.tickPosition == PlotEdge::Right && MX > plotRect.x + plotRect.w) ||
								   (p.yAxis.tickPosition == PlotEdge::Left && MX < plotRect.x));
				bool overY2Ticks = PlotHasSecondaryY(p) && !p.yAxis2.tickOverlay &&
								   ((p.yAxis2.tickPosition == PlotEdge::Right && MX > plotRect.x + plotRect.w) ||
									(p.yAxis2.tickPosition == PlotEdge::Left && MX < plotRect.x));
				if (overXTicks) {
					ApplyPlotAxisZoom(p.xAxis, p.series, true, plotRect.x, plotRect.x + plotRect.w, MX, in.wheelY);
				} else if (overYTicks) {
					ApplyPlotAxisZoom(p.yAxis, p.series, false, plotRect.y + plotRect.h, plotRect.y, MY, in.wheelY);
				} else if (overY2Ticks) {
					ApplyPlotAxisZoom(p.yAxis2, p.series, false, plotRect.y + plotRect.h, plotRect.y, MY, in.wheelY,
									  true);
				} else {
					ApplyPlotAxisZoom(p.xAxis, p.series, true, plotRect.x, plotRect.x + plotRect.w, MX, in.wheelY);
					ApplyPlotAxisZoom(p.yAxis, p.series, false, plotRect.y + plotRect.h, plotRect.y, MY, in.wheelY);
					if (PlotHasSecondaryY(p))
						ApplyPlotAxisZoom(p.yAxis2, p.series, false, plotRect.y + plotRect.h, plotRect.y, MY,
										  in.wheelY, true);
				}
				wheelConsumed = true;
				return;
			}

			PlotAxis xr = ResolveAxis(p.xAxis, p.series, true);
			PlotAxis yr = ResolveAxis(p.yAxis, p.series, false);
			PlotAxis yr2 = ResolveAxis(p.yAxis2, p.series, false, true);
			auto [si, idx] = PlotNearestPoint(p, plotRect, xr, yr, yr2, {MX, MY});
			p.hoveredSeries = si;
			p.hoveredIndex = idx;
		});

		// ── UiCalendar (Phase 8, DatePicker) : en-tête (flèches précédent/
		// suivant, mutation directe de month/year — pas de add_component,
		// sûr en ligne) change de mois ; clic sur une case pose selectedDay
		// et notifie onDaySelected (différé via `pending`, comme tout
		// callback applicatif — cf. le commentaire de dispatch()).
		world.Query<UiCalendar, UiComputed>([&](ecs::Entity e, UiCalendar &cal, UiComputed &c) {
			sdl3::FPoint p{MX, MY};
			bool overWidget = c.screen.Contains(p) && c.clip.Contains(p) && IsFrontMost(e) &&
							  !IsHiddenRecursive(world, e) &&
							  !IsDisabledRecursive(world, e);
			cal.hoveredDay = -1;
			cal.hoveredPrevArrow = false;
			cal.hoveredNextArrow = false;
			if (!overWidget)
				return;
			const float HEADER_H = 28.f;
			sdl3::FRect header{c.screen.x, c.screen.y, c.screen.w, HEADER_H};
			sdl3::FRect prevArrow{c.screen.x, c.screen.y, 28.f, HEADER_H};
			sdl3::FRect nextArrow{c.screen.x + c.screen.w - 28.f, c.screen.y, 28.f, HEADER_H};
			if (header.Contains(p)) {
				cal.hoveredPrevArrow = prevArrow.Contains(p);
				cal.hoveredNextArrow = nextArrow.Contains(p);
				if (PRESSED) {
					if (cal.hoveredPrevArrow) {
						if (--cal.month < 1) {
							cal.month = 12;
							--cal.year;
						}
					} else if (cal.hoveredNextArrow) {
						if (++cal.month > 12) {
							cal.month = 1;
							++cal.year;
						}
					}
				}
				return;
			}
			sdl3::FRect grid{c.screen.x, c.screen.y + HEADER_H, c.screen.w, sdl3::Max(0.f, c.screen.h - HEADER_H)};
			if (!grid.Contains(p))
				return;
			float cellW = grid.w / 7.f, cellH = grid.h / 6.f;
			int col = sdl3::Clamp(int((MX - grid.x) / sdl3::Max(1.f, cellW)), 0, 6);
			int row = sdl3::Clamp(int((MY - grid.y) / sdl3::Max(1.f, cellH)), 0, 5);
			int offset = FirstWeekdayOfMonth(cal.year, cal.month);
			int dayIdx = row * 7 + col - offset + 1;
			int dim = DaysInMonth(cal.year, cal.month);
			if (dayIdx >= 1 && dayIdx <= dim) {
				cal.hoveredDay = dayIdx;
				if (PRESSED) {
					cal.selectedDay = dayIdx;
					if (cal.onDaySelected) {
						auto fn = cal.onDaySelected;
						int y = cal.year, m = cal.month, d = dayIdx;
						pending.push_back([fn, y, m, d] { fn(y, m, d); });
					}
				}
			}
		});

		// ── Champs de saisie (mono-ligne, texte enveloppé) ───────────────────
		world.Query<UiInput, UiRect, UiComputed>([&](ecs::Entity e, UiInput &f, UiRect &r, UiComputed &c) {
			f.changed = false;
			f.submitted = false;
			f.hovered = hitOk(e, c);

			float fs = GetResolved(world, e).FontSize(14.f);
			float innerW = sdl3::Max(1.f, c.screen.w - 16.f);
			auto lines = WrapText(f.text, fs, innerW);

			// Clic : (re)focus + place curseur/ancre de sélection sous la
			// souris et démarre un glisser de sélection. Glisser en cours :
			// ne bouge que le curseur (l'ancre reste fixe).
			if (PRESSED) {
				f.focused = f.hovered;
				if (f.focused) {
					f.cursor = f.selectionAnchor = HitTestOffset(lines, f.text, fs, c.screen, r.scroll, MX, MY);
					textDrag = e;
				}
			} else if (textDrag == e && in.down) {
				f.cursor = HitTestOffset(lines, f.text, fs, c.screen, r.scroll, MX, MY);
			}
			if (in.released && textDrag == e)
				textDrag = ecs::Entity{};
			if (!f.focused)
				return;

			MoveCursor(f.text, lines, f.cursor, f.selectionAnchor, in);

			// enterInsertsNewline=false : Entrée soumet (ci-dessous), n'insère rien.
			Frame editIn = in;
			editIn.enter = false;
			f.changed = ApplyTextEdit(f.text, f.cursor, f.selectionAnchor, f.ioMode, f.maxLen, editIn, false);
			if (f.changed) {
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onTextChange) {
					auto fn = cb.Unwrap()->onTextChange;
					String v = f.text;
					pending.push_back([fn, v] { fn(v); });
				}
			}
			if (in.enter && IoCanWrite(f.ioMode)) {
				f.submitted = true;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onSubmit) {
					auto fn = cb.Unwrap()->onSubmit;
					String v = f.text;
					pending.push_back([fn, v] { fn(v); });
				}
			}
		});

		// ── Zones de texte multi-ligne ───────────────────────────────────────
		world.Query<UiInputArea, UiRect, UiComputed>([&](ecs::Entity e, UiInputArea &f, UiRect &r, UiComputed &c) {
			f.changed = false;
			f.hovered = hitOk(e, c);

			float fs = GetResolved(world, e).FontSize(14.f);
			auto lines = SplitLines(f.text);

			if (PRESSED) {
				f.focused = f.hovered;
				if (f.focused) {
					f.cursor = f.selectionAnchor = HitTestOffset(lines, f.text, fs, c.screen, r.scroll, MX, MY);
					textDrag = e;
				}
			} else if (textDrag == e && in.down) {
				f.cursor = HitTestOffset(lines, f.text, fs, c.screen, r.scroll, MX, MY);
			}
			if (in.released && textDrag == e)
				textDrag = ecs::Entity{};
			if (!f.focused)
				return;

			MoveCursor(f.text, lines, f.cursor, f.selectionAnchor, in);

			f.changed = ApplyTextEdit(f.text, f.cursor, f.selectionAnchor, f.ioMode, f.maxLen, in,
									  /*enterInsertsNewline=*/true);
			if (f.changed) {
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onTextChange) {
					auto fn = cb.Unwrap()->onTextChange;
					String v = f.text;
					pending.push_back([fn, v] { fn(v); });
				}
			}
		});

		// ── ListBox : sélection au clic, scroll interne à la molette ─────────
		world.Query<ListBox, UiComputed>([&](ecs::Entity e, ListBox &lb, UiComputed &c) {
			lb.hovered = hitOk(e, c);
			lb.hoveredItem = -1;
			lb.scroll = sdl3::Clamp(lb.scroll, 0.f, lb.MaxScroll(c.screen.h));
			if (lb.hovered && lb.itemHeight > 0.f) {
				int idx = int((MY - c.screen.y + lb.scroll) / lb.itemHeight);
				if (idx >= 0 && idx < int(lb.items.size()))
					lb.hoveredItem = idx;
				if (in.wheelY != 0.f && !wheelConsumed && lb.MaxScroll(c.screen.h) > 0.f) {
					lb.scroll = sdl3::Clamp(lb.scroll - in.wheelY * wheelSpeed, 0.f, lb.MaxScroll(c.screen.h));
					wheelConsumed = true;
				}
			}
			if (PRESSED && lb.hoveredItem >= 0 && lb.hoveredItem != lb.selected) {
				lb.selected = lb.hoveredItem;
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
					auto fn = cb.Unwrap()->onChange;
					float v = float(lb.selected);
					pending.push_back([fn, v] { fn(v); });
				}
			}
		});

		// ── Expander : clic sur l'en-tête = replier/déplier les enfants ──────
		// Le changement de visibilité (UiHidden) est structurel → différé
		// après la query.
		{
			std::vector<ecs::Entity> toggled;
			world.Query<UiExpander, UiComputed>([&](ecs::Entity e, UiExpander &x, UiComputed &c) {
				sdl3::FPoint p{MX, MY};
				sdl3::FRect header{c.screen.x, c.screen.y, c.screen.w, x.headerHeight};
				x.hovered = header.Contains(p) && c.clip.Contains(p) && IsFrontMost(e) &&
							!IsHiddenRecursive(world, e) &&
							!IsDisabledRecursive(world, e);
				if (PRESSED && x.hovered) {
					x.expanded = !x.expanded;
					toggled.push_back(e);
				}
			});
			for (ecs::Entity e : toggled) {
				auto x = world.GetComponent<UiExpander>(e);
				if (x.IsNone())
					continue;
				bool expanded = x.Unwrap()->expanded;
				if (auto ch = world.GetComponent<UiChildren>(e); ch.IsSome()) {
					std::vector<ecs::Entity> kids = ch.Unwrap()->list;
					for (ecs::Entity k : kids) {
						if (expanded)
							world.RemoveComponent<UiHidden>(k);
						else if (!world.HasComponent<UiHidden>(k))
							world.AddComponent(k, UiHidden{});
					}
				}
				layout.MarkDirty();
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onToggle) {
					auto fn = cb.Unwrap()->onToggle;
					pending.push_back([fn, expanded] { fn(expanded); });
				}
			}
		}

		// ── TabView : clic dans la barre = changer d'onglet actif ────────────
		{
			std::vector<ecs::Entity> switched;
			world.Query<UiTabView, UiComputed>([&](ecs::Entity e, UiTabView &tv, UiComputed &c) {
				tv.hoveredTab = -1;
				if (tv.tabs.empty())
					return;
				sdl3::FPoint p{MX, MY};
				sdl3::FRect bar{c.screen.x, c.screen.y, c.screen.w, tv.tabHeight};
				bool overBar = bar.Contains(p) && c.clip.Contains(p) && IsFrontMost(e) &&
							   !IsHiddenRecursive(world, e) &&
							   !IsDisabledRecursive(world, e);
				if (overBar) {
					float tabW = c.screen.w / float(tv.tabs.size());
					tv.hoveredTab =
						sdl3::Clamp(int((MX - c.screen.x) / sdl3::Max(1.f, tabW)), 0, int(tv.tabs.size()) - 1);
				}
				if (PRESSED && tv.hoveredTab >= 0 && tv.hoveredTab != tv.active) {
					tv.active = tv.hoveredTab;
					switched.push_back(e);
				}
			});
			for (ecs::Entity e : switched) {
				auto tv = world.GetComponent<UiTabView>(e);
				if (tv.IsNone())
					continue;
				int active = tv.Unwrap()->active;
				if (auto ch = world.GetComponent<UiChildren>(e); ch.IsSome()) {
					std::vector<ecs::Entity> kids = ch.Unwrap()->list;
					for (size_t i = 0; i < kids.size(); ++i) {
						if (int(i) == active)
							world.RemoveComponent<UiHidden>(kids[i]);
						else if (!world.HasComponent<UiHidden>(kids[i]))
							world.AddComponent(kids[i], UiHidden{});
					}
				}
				layout.MarkDirty();
				if (auto cb = world.GetComponent<UiCallbacks>(e); cb.IsSome() && cb.Unwrap()->onChange) {
					auto fn = cb.Unwrap()->onChange;
					float v = float(active);
					pending.push_back([fn, v] { fn(v); });
				}
			}
		}

		// ── Molette : scroll du conteneur scrollable le plus profond sous la souris ──
		if (in.wheelY != 0.f && !wheelConsumed) {
			ecs::Entity best{};
			float bestArea = 1e30f;
			world.Query<UiRect, UiComputed>([&](ecs::Entity e, UiRect &r, UiComputed &c) {
				if (r.MaxScroll().y <= 0.f && r.MaxScroll().x <= 0.f)
					return;
				if (!hitOk(e, c))
					return;
				float area = c.screen.w * c.screen.h;
				if (area < bestArea) {
					bestArea = area;
					best = e;
				}
			});
			if (best.Valid()) {
				if (auto r = world.GetComponent<UiRect>(best); r.IsSome()) {
					r.Unwrap()->scroll.y -= in.wheelY * wheelSpeed;
					r.Unwrap()->ClampScroll();
					layout.MarkDirty(); // le scroll déplace les enfants → re-place
				}
			}
		}

		// Callbacks applicatifs : exécutés maintenant que toutes les queries
		// de cet évènement sont terminées — sûr même s'ils mutent l'ECS
		// (changement de scène, despawn...).
		for (auto &fn : pending)
			fn();
	}
};

// ============================================================================
// RenderSystem
// ============================================================================

class RenderSystem {
public:
	/// Moteur de texte optionnel : sans lui, le texte est simplement omis
	/// (utile pour les tests headless). Pointeurs non-possédants.
	void SetTextEngine(sdl3::TextEngine *engine, sdl3::Font *font) {
		m_engine = engine;
		m_font = font;
	}

	/// Enregistre une police d'icônes sous son nom de famille logique (cf.
	/// ui::Glyphs::FontFamily<E>()) afin que les UiIcon qui la référencent
	/// s'y résolvent au rendu. Pointeur non-possédant, comme setTextEngine —
	/// la police doit vivre au moins aussi longtemps que le RenderSystem.
	void RegisterFont(StringView family, sdl3::Font &font) { iconFonts.insert_or_assign(String(family), &font); }

	/// Enregistre la police à utiliser pour le texte marqué `prop::Italic`
	/// (cf. WidgetBuilder::italic()/styles.hpp) — un objet sdl3::Font DISTINCT
	/// ouvert avec FontStyle::ITALIC, pas juste `.SetStyle()` sur la police
	/// normale (qui mettrait TOUT le texte en italique, cf. font étant
	/// unique et partagé par tous les widgets). Pointeur non-possédant,
	/// comme setTextEngine/registerFont. Sans appel, `prop::Italic` est posé
	/// sans effet (retombe sur la police normale, cf. drawWidget UiLabel).
	void RegisterItalicFont(sdl3::Font &font) { italicFont = &font; }
	/// Idem pour `prop::Bold` (gras).
	void RegisterBoldFont(sdl3::Font &font) { boldFont = &font; }
	/// Idem pour `prop::Bold` ET `prop::Italic` posés ensemble — sans elle,
	/// gras+italique retombe sur la police grasse seule (cf. pickTextFont()),
	/// éventuellement moins fidèle visuellement mais jamais absente.
	void RegisterBoldItalicFont(sdl3::Font &font) { boldItalicFont = &font; }

	/// Pool de textures pour UiImage (clé → texture).
	std::unordered_map<String, sdl3::Texture> textures;

	/// Texture d'effet composée par entité (M23, ui::ShaderEffectSystem,
	/// shader_effect.hpp) — SEULE copie possédée de chaque texture d'effet :
	/// UiShaderEffect (components.hpp) reste un composant pur, sans état GPU
	/// dessus (cf. sa doc). Alimenté par ShaderEffectSystem::Update, lu ici
	/// par DrawWidget/DrawTree ci-dessous. Une entrée absente (widget jamais
	/// traité par ShaderEffectSystem, ou dernier traitement en échec sans
	/// texture précédente) fait retomber DrawTree sur le rendu NORMAL du
	/// sous-arbre — dégradation "gratuite", cf. shader_effect.hpp en-tête.
	std::unordered_map<ecs::Entity, sdl3::Texture> effectTextures;

	// Couleurs des infobulles (indépendantes du thème de la factory).
	sdl3::FColor tooltipBg{25 / 255.f, 27 / 255.f, 38 / 255.f, 245 / 255.f};
	sdl3::FColor tooltipBorder{70 / 255.f, 76 / 255.f, 110 / 255.f, 1.f};
	sdl3::FColor tooltipText = sdl3::FColor::UI_TEXT_PRIMARY();

	// Couleurs de l'auto-scrollbar (conteneurs `.Scrollable()` en débordement).
	sdl3::FColor scrollbarTrack{1.f, 1.f, 1.f, 18 / 255.f};
	sdl3::FColor scrollbarThumb{1.f, 1.f, 1.f, 70 / 255.f};

	/// Dessine l'UI. `tip` (optionnel) : infobulle calculée par InputSystem,
	/// dessinée en overlay avec les listes des combos ouverts.
	void Run(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren, const InputSystem::Tooltip *tip = nullptr) {
		ren.SetBlendMode(sdl3::BlendMode::BLEND);
		std::vector<ecs::Entity> roots;
		world.Query<UiRect>([&](ecs::Entity e, UiRect &) {
			if (!world.HasComponent<UiParent>(e) && !world.HasComponent<UiHidden>(e))
				roots.push_back(e);
		});
		for (ecs::Entity root : roots)
			DrawTree(world, ren, root);
		ren.ClearClipRect();

		// ── Overlays : widgets AttachLayout::Fixed (détachés de l'arbre de
		// dessin normal pour flotter au premier plan — popups, modales, menus
		// déroulants, HUD, panneaux flottants...), puis listes des combos
		// ouverts, puis infobulle : chaque catégorie peut se superposer à la
		// précédente. Triés par UiOverlayLayer.order (défaut 0) plutôt que
		// dessinés dans l'ordre d'itération brut, pour qu'un sous-menu/popup
		// ouvert par-dessus une modale se dessine bien au-dessus d'elle.
		std::vector<std::pair<int, ecs::Entity>> fixedRoots;
		world.Query<UiItem, UiComputed>([&](ecs::Entity e, UiItem &it, UiComputed &) {
			if (it.attach == AttachLayout::FIXED && !IsHiddenRecursive(world, e)) {
				int order = 0;
				if (auto ol = world.GetComponent<UiOverlayLayer>(e); ol.IsSome())
					order = ol.Unwrap()->order;
				fixedRoots.push_back({order, e});
			}
		});
		std::stable_sort(fixedRoots.begin(), fixedRoots.end(),
						 [](const auto &a, const auto &b) { return a.first < b.first; });
		for (auto &[order, e] : fixedRoots)
			DrawTree(world, ren, e, /*overlayEntry=*/true);
		ren.ClearClipRect();

		world.Query<UiComboBox, UiComputed>([&](ecs::Entity e, UiComboBox &cb, UiComputed &c) {
			if (cb.open && !IsHiddenRecursive(world, e))
				DrawDropdown(ren, cb, c.screen, GetResolved(world, e));
		});
		if (tip && tip->visible && !tip->text.IsEmpty())
			DrawTooltip(ren, *tip);
	}

	void ClearTextCache() {
		textCache.clear();
		strCache.clear();
	}

private:
	sdl3::TextEngine *m_engine = nullptr;
	sdl3::Font *m_font = nullptr;
	// Variantes de police pour prop::Bold/Italic (cf. registerBoldFont() /
	// registerItalicFont() / registerBoldItalicFont()) — nullptr = non
	// enregistrée, cf. pickTextFont() pour la logique de repli.
	sdl3::Font *italicFont = nullptr;
	sdl3::Font *boldFont = nullptr;
	sdl3::Font *boldItalicFont = nullptr;
	/// Polices d'icônes additionnelles, indexées par nom de famille logique
	/// (cf. registerFont / ui::Glyphs::FontFamily). Pointeurs non-possédants.
	std::unordered_map<String, sdl3::Font *> iconFonts;

	struct CachedText {
		String str;
		sdl3::FColor color;
		const sdl3::Font *font = nullptr; ///< police utilisée à la création (invalide le cache si elle change)
		sdl3::Text text;
	};
	std::unordered_map<uint32_t, CachedText> textCache;
	// Cache secondaire clé = chaîne (items de listes, onglets, infobulle) —
	// borné par le nombre de chaînes distinctes affichées.
	struct CachedStr {
		sdl3::FColor color;
		sdl3::Text text;
	};
	std::unordered_map<String, CachedStr> strCache;

public:
	// `overlayEntry` : true uniquement pour l'appel qui (re-)lance le dessin
	// À LA RACINE d'un sous-arbre AttachLayout::Fixed depuis la passe overlay
	// de run(). Un Fixed rencontré pendant la descente normale (overlayEntry
	// = false, valeur par défaut) est sauté ici — il est dessiné séparément,
	// par-dessus tout le reste — pour éviter de le dessiner deux fois.
	//
	// PUBLIC (pas juste un détail interne de Run()) depuis M23 : ui::
	// ShaderEffectSystem (shader_effect.hpp) l'appelle directement sur UNE
	// entité précise pour capturer son sous-arbre dans une texture cible
	// dédiée, avant post-traitement GPU — cf. shader_effect.hpp en-tête pour
	// le pipeline complet.
	void DrawTree(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren, ecs::Entity e, bool overlayEntry = false) {
		if (world.HasComponent<UiHidden>(e))
			return;
		if (!overlayEntry) {
			if (auto it = world.GetComponent<UiItem>(e); it.IsSome() && it.Unwrap()->attach == AttachLayout::FIXED)
				return;
		}
		auto comp = world.GetComponent<UiComputed>(e);
		if (comp.IsNone())
			return;
		UiComputed c = *comp.Unwrap();
		if (c.clip.w <= 0.f || c.clip.h <= 0.f)
			return;

		ren.SetClipRect(sdl3::Rect{int(c.clip.x), int(c.clip.y), int(c.clip.w) + 1, int(c.clip.h) + 1});
		DrawWidget(world, ren, e, c);

		// UiShaderEffect (M23) avec texture prête : DrawWidget ci-dessus vient
		// de blitter le sous-arbre déjà post-traité (chrome + descendants
		// inclus, cf. shader_effect.hpp::ShaderEffectSystem::UpdateOne, étape
		// (a)) — descendre dans les enfants ici les dessinerait une SECONDE
		// fois, par-dessus. Absence d'entrée (pas encore traité, ou échec sans
		// texture précédente) : on descend normalement, comme n'importe quel
		// autre widget — dégradation "gratuite" (cf. shader_effect.hpp).
		bool subtreeAlreadyComposited = world.HasComponent<UiShaderEffect>(e) && effectTextures.contains(e);
		if (auto children = world.GetComponent<UiChildren>(e); children.IsSome() && !subtreeAlreadyComposited) {
			std::vector<ecs::Entity> kids = children.Unwrap()->list;
			for (ecs::Entity k : kids)
				DrawTree(world, ren, k);
		}

		// Auto-scrollbar : dès qu'il y a débordement réel, sans opt-in requis
		// (drawScrollbars ne dessine rien si maxScroll() == 0 sur les deux
		// axes) — par-dessus le contenu, bornée au clip de CE widget (déjà
		// actif ci-dessus), jamais celui, plus large, de ses enfants.
		if (auto r = world.GetComponent<UiRect>(e); r.IsSome()) {
			ren.SetClipRect(sdl3::Rect{int(c.clip.x), int(c.clip.y), int(c.clip.w) + 1, int(c.clip.h) + 1});
			DrawScrollbars(ren, *r.Unwrap(), c.screen);
		}

		// Sous-arbre désactivé : voile sombre par-dessus (une seule fois, à la
		// racine du marqueur UiDisabled).
		if (world.HasComponent<UiDisabled>(e)) {
			ren.SetClipRect(sdl3::Rect{int(c.clip.x), int(c.clip.y), int(c.clip.w) + 1, int(c.clip.h) + 1});
			ren.SetDrawColor(sdl3::FColor{15/255.f, 15/255.f, 20/255.f, 110/255.f});
			ren.FillRect(c.screen);
		}
	}

private:

	void DrawWidget(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren, ecs::Entity e, const UiComputed &c) {
		const sdl3::FRect &s = c.screen;

		if (world.HasComponent<UiPanel>(e)) {
			// UiPanel est un marqueur pur ("cette entité a un fond à
			// dessiner") — fond/dégradé/bordure/radius viennent entièrement
			// du style résolu (classes "root-panel"/inline, cf. styles.hpp).
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FColor bg = rs.Bg(sdl3::FColor{});
			Option<sdl3::FColor> grad = rs.BgGradientOpt();
			math::Corners radius = rs.BordersRadius(math::Corners{});
			bool hasBorder = rs.HasBorderColor();
			sdl3::FColor border = rs.BorderColor(sdl3::FColor{});
			math::Sides borderWidth = rs.BordersWidth(math::Sides(1.f));

			if (grad.IsSome()) {
				DrawVGradient(ren, s, bg, grad.Unwrap());
			} else if (bg.a > 0) {
				ren.SetDrawColor(bg);
				ren.FillRoundedRect(s, radius);
			}
			if (hasBorder) {
				ren.SetDrawColor(border);
				// Épaisseur : contours concentriques (1 trait par pixel).
				// `.top` sert de représentatif uniforme — le cas courant
				// (bordure identique sur les 4 côtés) reste exact ; un
				// borderWidth non-uniforme n'est pas encore rendu par côté.
				int bw = sdl3::Max(1, int(borderWidth.top));
				for (int i = 0; i < bw; ++i) {
					sdl3::FRect br{s.x + float(i), s.y + float(i), s.w - 2.f * float(i), s.h - 2.f * float(i)};
					if (br.w <= 0.f || br.h <= 0.f)
						break;
					float fi = float(i);
					math::Corners inset{sdl3::Max(0.f, radius.tl - fi), sdl3::Max(0.f, radius.tr - fi),
								  sdl3::Max(0.f, radius.bl - fi), sdl3::Max(0.f, radius.br - fi)};
					ren.DrawRoundedRect(br, inset);
				}
			}
			if (rs.HasGlow())
				DrawGlowRing(ren, s, radius.tl, rs.Glow(0.f), rs.GlowColor(sdl3::FColor::WHITE()));
			if (rs.HasGloss())
				DrawGlossHighlight(ren, s, radius.tl, rs.Gloss(0.f));
		}

		if (auto img = world.GetComponent<UiImage>(e); img.IsSome()) {
			auto it = textures.find(img.Unwrap()->textureKey.c_str());
			if (it != textures.end())
				DrawImageFit(ren, it->second, s, img.Unwrap()->fit);
		}

		// Contenu déjà entièrement rendu par Viewport3DSystem::Update, AVANT ce
		// passage de dessin 2D (cf. ui.hpp::Ui::Render() et viewport3d.hpp en-tête
		// de fichier pour l'ordre exact) — ici, un simple blit de la texture
		// résultat, comme UiImage juste au-dessus. displayTexture reste NONE tant
		// qu'aucune frame n'a pu être rendue (backend sans sdl3::Renderer réel,
		// widget pas encore passé par layout...) : dans ce cas on ne dessine
		// simplement rien ce frame-là (dégradation cohérente avec le reste du
		// dépôt). FILL (pas CONTAIN/COVER) : camera.aspect est déjà aligné sur les
		// dimensions EXACTES de l'OffscreenTarget par RenderObjectToTexture (M21),
		// donc la texture n'est jamais déformée par ce blit — FILL se contente de
		// la positionner pile sur le rectangle du widget, sans marges ni rognage.
		if (auto vp3 = world.GetComponent<UiViewport3D>(e); vp3.IsSome() && vp3.Unwrap()->displayTexture.IsSome())
			DrawImageFit(ren, *vp3.Unwrap()->displayTexture, s, ImageFit::FILL);

		// UiShaderEffect (M23, shader_effect.hpp) : même idée que UiViewport3D
		// juste au-dessus — le contenu (sous-arbre entier, chrome inclus) a
		// déjà été entièrement rendu ET post-traité par ShaderEffectSystem::
		// Update, AVANT ce passage de dessin 2D (cf. ui.hpp::Ui::Render()) ;
		// ici, un simple blit, comme UiImage/UiViewport3D. `effectTextures`
		// (pas le composant, cf. sa doc) porte la texture — absente tant
		// qu'aucun frame n'a pu être post-traité avec succès (dégradation :
		// DrawTree ci-dessus descend alors normalement dans les enfants à la
		// place, cf. son propre commentaire).
		if (world.HasComponent<UiShaderEffect>(e)) {
			if (auto it = effectTextures.find(e); it != effectTextures.end())
				DrawImageFit(ren, it->second, s, ImageFit::FILL);
		}

		if (auto ic = world.GetComponent<UiIcon>(e); ic.IsSome()) {
			const UiIcon &icon = *ic.Unwrap();
			sdl3::Font *f = m_font;
			if (auto fit = iconFonts.find(icon.font); fit != iconFonts.end())
				f = fit->second;
			sdl3::FColor tint = GetResolved(world, e).TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			DrawTextCentered(ren, e, icon.glyph, tint, s, TextAlign::Center, f);
		}

		if (auto b = world.GetComponent<UiButton>(e); b.IsSome()) {
			const UiButton &btn = *b.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FColor normal = rs.Bg(sdl3::FColor{40/255.f, 44/255.f, 64/255.f, 255/255.f});
			sdl3::FColor hoveredC = rs.BgHovered(sdl3::FColor::UI_BORDER_MUTED2());
			sdl3::FColor pressedC = rs.BgPressed(sdl3::FColor{24/255.f, 26/255.f, 40/255.f, 255/255.f});
			sdl3::FColor bg = btn.pressed ? pressedC : (btn.hovered ? hoveredC : normal);
			math::Corners radius = rs.BordersRadius(math::Corners(6.f));

			if (auto grad = rs.BgGradientOpt(); grad.IsSome())
				DrawVGradient(ren, s, bg, grad.Unwrap());
			else {
				ren.SetDrawColor(bg);
				ren.FillRoundedRect(s, radius);
			}
			sdl3::FColor borderNormal = rs.BorderColor(sdl3::FColor{0/255.f, 0/255.f, 0/255.f, 120/255.f});
			sdl3::FColor borderHover = rs.BorderFocus(sdl3::FColor{255/255.f, 255/255.f, 255/255.f, 60/255.f});
			ren.SetDrawColor(btn.hovered ? borderHover : borderNormal);
			ren.DrawRoundedRect(s, radius);
			if (rs.HasGlow())
				DrawGlowRing(ren, s, radius.tl, rs.Glow(0.f), rs.GlowColor(sdl3::FColor::WHITE()));
			if (rs.HasGloss())
				DrawGlossHighlight(ren, s, radius.tl, rs.Gloss(0.f));
			DrawTextCentered(ren, e, btn.text, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), s, TextAlign::Center);
		}

		if (auto l = world.GetComponent<UiLabel>(e); l.IsSome()) {
			ResolvedStyle rs = GetResolved(world, e);
			const String &text = l.Unwrap()->text;
			sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			TextAlign align = rs.TextAlign(TextAlign::Left);
			float fs = rs.FontSize(14.f);
			sdl3::Font *useFont = PickTextFont(rs.Bold(false), rs.Italic(false));

			bool hl = rs.Highlight(false), ul = rs.Underline(false), st = rs.Strikethrough(false);
			bool needsDecoration = (hl || ul || st) && !text.IsEmpty();
			sdl3::FPoint tsz{};
			float tx = s.x, ty = s.y;
			if (needsDecoration) {
				// Reproduit l'alignement interne de drawTextCentered pour
				// positionner les décorations exactement sous/sur/derrière le
				// texte réellement dessiné (pas de valeur de retour exposée
				// par drawTextCentered — dupliqué ici plutôt que de changer
				// sa signature, utilisée ailleurs sans ce besoin).
				tsz = MeasureCached(text, fs);
				tx = s.x + 8.f;
				if (align == TextAlign::Center)
					tx = s.x + (s.w - tsz.x) * 0.5f;
				else if (align == TextAlign::Right)
					tx = s.x + s.w - tsz.x - 8.f;
				ty = s.y + (s.h - tsz.y) * 0.5f;
			}
			if (hl && needsDecoration) {
				ren.SetDrawColor(rs.HighlightColor(sdl3::FColor{255/255.f, 230/255.f, 90/255.f, 130/255.f}));
				ren.FillRect({tx - 2.f, ty - 1.f, tsz.x + 4.f, tsz.y + 2.f});
			}

			DrawTextCentered(ren, e, text, textColor, s, align, useFont);

			if (needsDecoration) {
				ren.SetDrawColor(textColor);
				if (ul)
					ren.FillRect({tx, ty + tsz.y - 2.f, tsz.x, 1.5f});
				if (st)
					ren.FillRect({tx, ty + tsz.y * 0.5f, tsz.x, 1.5f});
			}
		}

		if (auto t = world.GetComponent<UiToggle>(e); t.IsSome()) {
			const UiToggle &tg = *t.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			float r = s.h * 0.5f;
			sdl3::FColor track = tg.checked ? rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE())
									  : (tg.hovered ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
			ren.SetDrawColor(track);
			ren.FillRoundedRect(s, math::Corners(r));
			float knobR = r - 3.f;
			float kx = sdl3::Lerp(s.x + r, s.x + s.w - r, tg.animT);
			ren.SetDrawColor(rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()));
			ren.FillCircle({kx, s.y + r}, knobR);
		}

		if (auto cb = world.GetComponent<UiCheckbox>(e); cb.IsSome()) {
			const UiCheckbox &ck = *cb.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FColor bg = ck.checked ? rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE())
								   : (ck.hovered ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
			ren.SetDrawColor(bg);
			ren.FillRoundedRect(s, math::Corners(4.f));
			if (ck.checked) {
				ren.SetDrawColor(rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()));
				float x = s.x, y = s.y, w = s.w, h = s.h;
				ren.DrawLine(x + w * 0.22f, y + h * 0.52f, x + w * 0.42f, y + h * 0.72f);
				ren.DrawLine(x + w * 0.42f, y + h * 0.72f, x + w * 0.78f, y + h * 0.30f);
			}
		}

		if (auto sl = world.GetComponent<UiSlider>(e); sl.IsSome()) {
			const UiSlider &s2 = *sl.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FColor cNormal = rs.Bg(sdl3::FColor::UI_PANEL_DARK());
			sdl3::FColor cFill = rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE());
			sdl3::FColor cThumb = s2.dragging ? rs.BgPressed(sdl3::FColor::UI_ACCENT_BLUE()) : rs.BgHovered(sdl3::FColor::UI_ACCENT_BLUE_LIGHT());
			float t = s2.Normalized();
			if (s2.orient == Orientation::Horizontal) {
				sdl3::FRect track{s.x, s.y + s.h * 0.5f - 3.f, s.w, 6.f};
				ren.SetDrawColor(cNormal);
				ren.FillRoundedRect(track, math::Corners(3.f));
				sdl3::FRect fill{track.x, track.y, track.w * t, track.h};
				ren.SetDrawColor(cFill);
				ren.FillRoundedRect(fill, math::Corners(3.f));
				ren.SetDrawColor(cThumb);
				ren.FillCircle({s.x + s.w * t, s.y + s.h * 0.5f}, s.h * 0.5f);
			} else {
				sdl3::FRect track{s.x + s.w * 0.5f - 3.f, s.y, 6.f, s.h};
				ren.SetDrawColor(cNormal);
				ren.FillRoundedRect(track, math::Corners(3.f));
				sdl3::FRect fill{track.x, track.y + track.h * (1.f - t), track.w, track.h * t};
				ren.SetDrawColor(cFill);
				ren.FillRoundedRect(fill, math::Corners(3.f));
				ren.SetDrawColor(cThumb);
				ren.FillCircle({s.x + s.w * 0.5f, s.y + s.h * (1.f - t)}, s.w * 0.5f);
			}
		}

		if (auto pr = world.GetComponent<UiProgress>(e); pr.IsSome()) {
			const UiProgress &p2 = *pr.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			math::Corners radius = rs.BordersRadius(math::Corners(4.f));
			ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
			ren.FillRoundedRect(s, radius);
			sdl3::FRect fill{s.x, s.y, s.w * p2.Normalized(), s.h};
			ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE()));
			ren.FillRoundedRect(fill, radius);
		}

		if (world.HasComponent<UiSeparator>(e)) {
			ren.SetDrawColor(GetResolved(world, e).Bg(sdl3::FColor::UI_BORDER_MUTED()));
			ren.FillRect(s);
		}

		if (auto in = world.GetComponent<UiInput>(e); in.IsSome()) {
			const UiInput &f = *in.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			float fs = rs.FontSize(14.f);
			sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			sdl3::FColor selColor = rs.BgChecked(sdl3::FColor::WHITE());
			ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_PANEL_DARKER()));
			ren.FillRoundedRect(s, math::Corners(4.f));
			ren.SetDrawColor(f.focused ? rs.BorderFocus(sdl3::FColor::UI_ACCENT_BLUE_HOVER()) : rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
			ren.DrawRoundedRect(s, math::Corners(4.f));
			bool empty = f.text.IsEmpty();
			if (empty) {
				DrawTextCentered(ren, e, f.placeholder, rs.BgHovered(sdl3::FColor::UI_TEXT_MUTED()), s, TextAlign::Left);
			} else {
				sdl3::FPoint scroll{};
				if (auto r = world.GetComponent<UiRect>(e); r.IsSome())
					scroll = r.Unwrap()->scroll;
				float innerW = sdl3::Max(1.f, s.w - 16.f);
				std::vector<TextLine> lines = WrapText(f.text, fs, innerW);
				float lh = LineHeightApprox(fs);
				DrawSelection(ren, lines, fs, f.cursor, f.selectionAnchor, s, scroll, selColor);
				float y = s.y + 4.f - scroll.y;
				for (const TextLine &line : lines) {
					if (y + lh >= s.y && y <= s.y + s.h)
						DrawTextRaw(ren, line.text, textColor, s.x + 8.f, y, lh);
					y += lh;
				}
				if (f.focused && sdl3::Fmod(f.blink, 1.f) < 0.5f)
					DrawCaret(ren, lines, fs, f.cursor, s, scroll, selColor);
			}
		}

		if (auto ina = world.GetComponent<UiInputArea>(e); ina.IsSome()) {
			const UiInputArea &f = *ina.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			float fs = rs.FontSize(14.f);
			sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			sdl3::FColor selColor = rs.BgChecked(sdl3::FColor::WHITE());
			ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_PANEL_DARKER()));
			ren.FillRoundedRect(s, math::Corners(4.f));
			ren.SetDrawColor(f.focused ? rs.BorderFocus(sdl3::FColor::UI_ACCENT_BLUE_HOVER()) : rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
			ren.DrawRoundedRect(s, math::Corners(4.f));
			bool empty = f.text.IsEmpty();
			if (empty) {
				DrawTextRaw(ren, f.placeholder, rs.BgHovered(sdl3::FColor::UI_TEXT_MUTED()), s.x + 8.f, s.y + 4.f,
						   LineHeightApprox(fs));
			} else {
				sdl3::FPoint scroll{};
				if (auto r = world.GetComponent<UiRect>(e); r.IsSome())
					scroll = r.Unwrap()->scroll;
				std::vector<TextLine> lines = SplitLines(f.text);
				float lh = LineHeightApprox(fs);
				DrawSelection(ren, lines, fs, f.cursor, f.selectionAnchor, s, scroll, selColor);
				float y = s.y + 4.f - scroll.y;
				for (const TextLine &line : lines) {
					if (y + lh >= s.y && y <= s.y + s.h)
						DrawTextRaw(ren, line.text, textColor, s.x + 8.f - scroll.x, y, lh);
					y += lh;
				}
				if (f.focused && sdl3::Fmod(f.blink, 1.f) < 0.5f)
					DrawCaret(ren, lines, fs, f.cursor, s, scroll, selColor);
			}
		}

		if (auto dv = world.GetComponent<UiDragValue>(e); dv.IsSome()) {
			const UiDragValue &d = *dv.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			float fs = rs.FontSize(14.f);
			ren.SetDrawColor(rs.Bg(sdl3::FColor{28/255.f, 30/255.f, 44/255.f, 1.f}));
			ren.FillRoundedRect(s, math::Corners(4.f));
			ren.SetDrawColor(d.editing ? rs.BorderFocus(sdl3::FColor{120/255.f, 170/255.f, 250/255.f, 1.f}) : rs.BorderColor(sdl3::FColor{70/255.f, 76/255.f, 110/255.f, 1.f}));
			ren.DrawRoundedRect(s, math::Corners(4.f));
			if (!d.editing) {
				// Bande de remplissage proportionnelle à la valeur (repère
				// visuel façon slider, sans thumb — glisser reste possible
				// partout dans le widget, pas juste sur la bande).
				float t = (d.max > d.min) ? sdl3::Clamp((d.value - d.min) / (d.max - d.min), 0.f, 1.f) : 0.f;
				sdl3::FColor fill = rs.BgChecked(sdl3::FColor{70/255.f, 130/255.f, 210/255.f, 1.f});
				ren.SetDrawColor(sdl3::FColor{fill.r, fill.g, fill.b, fill.a / 4.f});
				ren.FillRoundedRect({s.x, s.y, s.w * t, s.h}, math::Corners(4.f));
				DrawTextCentered(ren, e, d.Formatted(), rs.TextColor(sdl3::FColor{220/255.f, 222/255.f, 232/255.f, 1.f}), s, TextAlign::Center);
			} else {
				sdl3::FColor textColor = rs.TextColor(sdl3::FColor{220/255.f, 222/255.f, 232/255.f, 1.f});
				DrawTextRaw(ren, d.editText, textColor, s.x + 8.f, s.y + (s.h - LineHeightApprox(fs)) * 0.5f,
						   LineHeightApprox(fs));
				if (sdl3::Fmod(d.blink, 1.f) < 0.5f) {
					float caretX = s.x + 8.f + float(d.editText.size()) * CharWidthApprox(fs);
					ren.SetDrawColor(textColor);
					ren.FillRect({caretX, s.y + 3.f, 1.5f, s.h - 6.f});
				}
			}
		}

		if (auto sw = world.GetComponent<UiColorSwatch>(e); sw.IsSome()) {
			const UiColorSwatch &swatch = *sw.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			math::Corners radius = rs.BordersRadius(math::Corners(4.f));
			if (swatch.color.a < 255)
				DrawCheckerboard(ren, s, radius);
			if (swatch.color.a > 0) {
				ren.SetDrawColor(swatch.color);
				ren.FillRoundedRect(s, radius);
			}
			ren.SetDrawColor(swatch.hovered ? rs.BorderFocus(sdl3::FColor{1.f, 1.f, 1.f, 160/255.f})
											: rs.BorderColor(sdl3::FColor{0.f, 0.f, 0.f, 120/255.f}));
			ren.DrawRoundedRect(s, radius);
		}

		if (auto svp = world.GetComponent<UiSVSquare>(e); svp.IsSome()) {
			const UiSVSquare &sv = *svp.Unwrap();
			sdl3::FColor hueColor = HsvToColor(sv.h, 1.f, 1.f);
			DrawQuadGradient(ren, s, sdl3::FColor::WHITE(), hueColor, sdl3::FColor::BLACK(), sdl3::FColor::BLACK());
			float cx = s.x + sv.s * s.w, cy = s.y + (1.f - sv.v) * s.h;
			ren.SetDrawColor(sdl3::FColor::WHITE());
			ren.DrawCircle({cx, cy}, 6.f);
			ren.SetDrawColor(sdl3::FColor::BLACK());
			ren.DrawCircle({cx, cy}, 5.f);
		}

		if (auto hsp = world.GetComponent<UiHueSlider>(e); hsp.IsSome()) {
			const UiHueSlider &h = *hsp.Unwrap();
			DrawHueBar(ren, s);
			float cx = s.x + (h.hue / 360.f) * s.w;
			ren.SetDrawColor(sdl3::FColor::WHITE());
			ren.FillRect({cx - 2.f, s.y - 2.f, 4.f, s.h + 4.f});
			ren.SetDrawColor(sdl3::FColor::BLACK());
			ren.DrawRect({cx - 2.f, s.y - 2.f, 4.f, s.h + 4.f});
		}

		if (auto asp = world.GetComponent<UiAlphaSlider>(e); asp.IsSome()) {
			const UiAlphaSlider &a = *asp.Unwrap();
			DrawCheckerboard(ren, s, math::Corners{});
			DrawHGradient(ren, s, sdl3::FColor{a.baseColor.r, a.baseColor.g, a.baseColor.b, 0.f},
						  sdl3::FColor{a.baseColor.r, a.baseColor.g, a.baseColor.b, 1.f});
			float cx = s.x + a.alpha * s.w;
			ren.SetDrawColor(sdl3::FColor::WHITE());
			ren.FillRect({cx - 2.f, s.y - 2.f, 4.f, s.h + 4.f});
			ren.SetDrawColor(sdl3::FColor::BLACK());
			ren.DrawRect({cx - 2.f, s.y - 2.f, 4.f, s.h + 4.f});
		}

		if (auto mb = world.GetComponent<UiMenuBarItem>(e); mb.IsSome()) {
			const UiMenuBarItem &m = *mb.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			bool open = false;
			if (m.menuPopup.Valid())
				if (auto ps = world.GetComponent<UiPopupState>(m.menuPopup); ps.IsSome())
					open = ps.Unwrap()->open;
			// Au repos : le fond RÉSOLU du widget (classe « root-menubaritem »,
			// donc thémé), transparent si le thème n'en définit pas. L'ancien
			// `FColor::BLACK()` peignait un rectangle NOIR OPAQUE quel que
			// soit le thème — invisible sur fond sombre, mais une barre de
			// menus illisible en thème clair (texte foncé sur noir).
			sdl3::FColor bg = open ? rs.BgPressed(sdl3::FColor::UI_BORDER_MUTED2())
								   : (m.hovered ? rs.BgHovered(sdl3::FColor{55 / 255.f, 60 / 255.f, 84 / 255.f, 1.f})
												: rs.Bg(sdl3::FColor{0.f, 0.f, 0.f, 0.f}));
			if (bg.a > 0) {
				ren.SetDrawColor(bg);
				ren.FillRoundedRect(s, rs.BordersRadius(math::Corners(4.f)));
			}
			DrawTextCentered(ren, e, m.text, rs.TextColor(sdl3::FColor{220/255.f, 222/255.f, 232/255.f, 1.f}), s, TextAlign::Center);
		}

		if (auto mi = world.GetComponent<UiMenuItem>(e); mi.IsSome()) {
			const UiMenuItem &m = *mi.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			bool submenuOpen = false;
			if (m.submenuPopup.Valid())
				if (auto ps = world.GetComponent<UiPopupState>(m.submenuPopup); ps.IsSome())
					submenuOpen = ps.Unwrap()->open;
			sdl3::FColor bg = (m.hovered || submenuOpen) ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : sdl3::FColor::BLACK();
			if (bg.a > 0) {
				ren.SetDrawColor(bg);
				ren.FillRoundedRect(s, rs.BordersRadius(math::Corners(4.f)));
			}
			sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			float arrowW = m.hasSubmenu ? 16.f : 0.f;
			sdl3::FRect textBox{s.x, s.y, sdl3::Max(0.f, s.w - arrowW), s.h};
			DrawTextCentered(ren, e, m.text, textColor, textBox, TextAlign::Left);
			if (!m.shortcut.IsEmpty()) {
				float fs = rs.FontSize(14.f);
				sdl3::FColor muted = rs.BgHovered(sdl3::FColor{150/255.f, 156/255.f, 178/255.f, 255/255.f});
				sdl3::FPoint sz = MeasureCached(m.shortcut, fs);
				DrawTextRaw(ren, m.shortcut, muted, s.x + s.w - arrowW - sz.x - 8.f, s.y, s.h);
			}
			if (m.hasSubmenu) {
				float cx = s.x + s.w - 10.f, cy = s.y + s.h * 0.5f;
				std::array<sdl3::Vertex, 3> tri{{
					{{cx - 3.f, cy - 5.f}, textColor, {}},
					{{cx - 3.f, cy + 5.f}, textColor, {}},
					{{cx + 4.f, cy}, textColor, {}},
				}};
				ren.RenderGeometry(tri);
			}
		}

		if (auto tbo = world.GetComponent<UiTable>(e); tbo.IsSome()) {
			const UiTable &t = *tbo.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			float fs = rs.FontSize(14.f);
			ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_BG_DEEP()));
			ren.FillRoundedRect(s, math::Corners(4.f));
			ren.SetDrawColor(rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
			ren.DrawRoundedRect(s, math::Corners(4.f));

			// ── En-tête ────────────────────────────────────────────────────
			sdl3::FRect headerRect{s.x, s.y, s.w, t.headerHeight};
			ren.SetDrawColor(rs.BgHovered(sdl3::FColor{36/255.f, 40/255.f, 58/255.f, 255/255.f}));
			ren.FillRect(headerRect);
			for (int i = 0; i < int(t.columns.size()); ++i) {
				const UiTableColumn &col = t.columns[size_t(i)];
				float x = s.x + t.ColumnX(i);
				sdl3::FRect cell{x, s.y, col.width, t.headerHeight};
				if (i == t.hoveredColumn) {
					ren.SetDrawColor(rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()));
					ren.FillRect(cell);
				}
				sdl3::FPoint tsz = MeasureCached(col.title, fs);
				DrawTextRaw(ren, col.title, textColor, x + 8.f, s.y, t.headerHeight);
				if (i == t.sortColumn && t.sortOrder != SortOrder::NONE) {
					float ax = x + 8.f + tsz.x + 6.f, ay = s.y + t.headerHeight * 0.5f;
					bool asc = t.sortOrder == SortOrder::ASCENDING;
					std::array<sdl3::Vertex, 3> tri =
						asc ? std::array<sdl3::Vertex, 3>{{{{ax - 4.f, ay + 2.f}, textColor, {}},
														  {{ax + 4.f, ay + 2.f}, textColor, {}},
														  {{ax, ay - 3.f}, textColor, {}}}}
							: std::array<sdl3::Vertex, 3>{{{{ax - 4.f, ay - 2.f}, textColor, {}},
														  {{ax + 4.f, ay - 2.f}, textColor, {}},
														  {{ax, ay + 3.f}, textColor, {}}}};
					ren.RenderGeometry(tri);
				}
				if (i > 0) {
					ren.SetDrawColor(rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
					ren.DrawLine(x, s.y + 4.f, x, s.y + t.headerHeight - 4.f);
				}
			}

			// ── Corps (lignes visibles seulement — clip resserré) ────────────
			sdl3::FRect bodyRect{s.x, s.y + t.headerHeight, s.w, sdl3::Max(0.f, s.h - t.headerHeight)};
			sdl3::FRect inner = c.clip.Intersection(bodyRect);
			if (inner.w > 0.f && inner.h > 0.f && t.rowHeight > 0.f && !t.rows.empty()) {
				ren.SetClipRect(sdl3::Rect{int(inner.x), int(inner.y), int(inner.w) + 1, int(inner.h) + 1});
				int first = sdl3::Max(0, int(t.scroll / t.rowHeight));
				int last = sdl3::Min(int(t.rows.size()) - 1, int((t.scroll + bodyRect.h) / t.rowHeight));
				bool selMulti = false;
				const std::unordered_set<int> *selectedSet = nullptr;
				if (auto us = world.GetComponent<UiSelection>(e); us.IsSome()) {
					selectedSet = &us.Unwrap()->state.selected;
					selMulti = us.Unwrap()->multiSelect;
				}
				(void)selMulti;
				for (int row = first; row <= last; ++row) {
					float ry = bodyRect.y + float(row) * t.rowHeight - t.scroll;
					sdl3::FRect rowRect{s.x, ry, s.w, t.rowHeight};
					bool isSelected = selectedSet && selectedSet->contains(row);
					if (isSelected) {
						ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_GLOW()));
						ren.FillRect(rowRect);
					} else if (row == t.hoveredRow) {
						ren.SetDrawColor(rs.BgHovered(sdl3::FColor{255/255.f, 255/255.f, 255/255.f, 18/255.f}));
						ren.FillRect(rowRect);
					}
					const auto &rowData = t.rows[size_t(row)];
					for (int col = 0; col < int(t.columns.size()) && col < int(rowData.size()); ++col) {
						float cx = s.x + t.ColumnX(col) + 8.f;
						DrawTextRaw(ren, rowData[size_t(col)], textColor, cx, ry, t.rowHeight);
					}
				}
				float maxSc = t.MaxScroll(bodyRect.h);
				if (maxSc > 0.f) {
					float ratio = sdl3::Clamp(bodyRect.h / t.ContentHeight(), 0.05f, 1.f);
					float thumbH = bodyRect.h * ratio;
					float thumbY = bodyRect.y + (bodyRect.h - thumbH) * (t.scroll / maxSc);
					ren.SetDrawColor(rs.BgPressed(sdl3::FColor::UI_ACCENT_BLUE_LIGHT()));
					ren.FillRoundedRect({s.x + s.w - 6.f, thumbY, 4.f, thumbH}, math::Corners(2.f));
				}
				ren.SetClipRect(sdl3::Rect{int(c.clip.x), int(c.clip.y), int(c.clip.w) + 1, int(c.clip.h) + 1});
			}
		}

		if (auto po = world.GetComponent<UiPlot>(e); po.IsSome()) {
			const UiPlot &p = *po.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_BG_DEEP()));
			ren.FillRoundedRect(s, math::Corners(4.f));
			ren.SetDrawColor(rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
			ren.DrawRoundedRect(s, math::Corners(4.f));

			ren.SetClipRect(sdl3::Rect{int(s.x), int(s.y), int(s.w) + 1, int(s.h) + 1});
			sdl3::FRect plotRect = ComputePlotRect(p, s);
			sdl3::FColor tickTextColor = rs.TextColor(sdl3::FColor{190/255.f, 194/255.f, 210/255.f, 255/255.f});
			float tickFs = rs.FontSize(11.f);

			if (p.mode == PlotMode::PIE) {
				// ── PlotMode::PIE (Phase 5) : pas d'axes/grille/graduations —
				// juste le camembert et sa légende (parts = entrées).
				sdl3::FRect circleRect = ComputePieCircleRect(plotRect);
				sdl3::FPoint center{circleRect.x + circleRect.w * 0.5f, circleRect.y + circleRect.h * 0.5f};
				float radius = circleRect.w * 0.5f;
				auto angles = ComputePieAngles(p.pieSlices);
				// La légende prime sur le survol direct pour la surbrillance
				// (même hiérarchie que XY : legendHover dicte le dimFactor).
				int hoveredSlice = p.legendHover >= 0 ? p.legendHover : p.hoveredSeries;
				DrawPieChart(ren, p.pieSlices, angles, center, radius, hoveredSlice,
							rs.BorderColor(sdl3::FColor{20/255.f, 21/255.f, 28/255.f, 220/255.f}));

				// ── Étiquette de la part survolée (légende ou tracé) ─────────
				if (hoveredSlice >= 0 && size_t(hoveredSlice) < p.pieSlices.size()) {
					const PieSlice &hs = p.pieSlices[size_t(hoveredSlice)];
					float total = 0.f;
					for (const PieSlice &sl : p.pieSlices)
						if (sl.visible)
							total += sdl3::Max(0.f, sl.value);
					float pct = total > 0.f ? sdl3::Max(0.f, hs.value) / total * 100.f : 0.f;
					String full = hs.label.IsEmpty() ? String::From(hs.value, 1) : hs.label;
					full.Append(" (");
					full.Append(String::From(pct, 1));
					full.Append("%)");
					float fs = rs.FontSize(12.f);
					sdl3::FPoint tsz = MeasureCached(full, fs);
					float tx = sdl3::Clamp(s.x + s.w * 0.5f - tsz.x * 0.5f, s.x, sdl3::Max(s.x, s.x + s.w - tsz.x));
					DrawTextRaw(ren, full, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), tx, s.y + 2.f, tsz.y);
				}

				// ── Légende (fond+pastilles via plot.hpp, texte ici) ─────────
				if (p.showLegend && !p.pieSlices.empty()) {
					sdl3::FRect legendBox = LegendBoxRect(p, s, plotRect);
					DrawPlotLegendBackground(ren, p, legendBox);
					float legendFs = rs.FontSize(11.f);
					for (size_t i = 0; i < p.pieSlices.size(); ++i) {
						if (p.pieSlices[i].label.IsEmpty())
							continue;
						sdl3::FRect labelRect = LegendLabelRect(LegendRowRect(legendBox, i));
						sdl3::FColor lc = tickTextColor;
						if (!p.pieSlices[i].visible)
							lc.a = lc.a * 0.5f;
						sdl3::FPoint tsz = MeasureCached(p.pieSlices[i].label, legendFs);
						DrawTextRaw(ren, p.pieSlices[i].label, lc, labelRect.x,
									labelRect.y + (labelRect.h - tsz.y) * 0.5f, legendFs);
					}
				}

				ren.ClearClipRect();
				if (!p.title.IsEmpty())
					DrawTextCentered(ren, e, p.title, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), sdl3::FRect{s.x, s.y + 2.f, s.w, 16.f},
									 TextAlign::Center);
				return;
			}

			if (p.mode == PlotMode::HEATMAP) {
				// ── PlotMode::HEATMAP (Phase 6) : pas d'axes/légende — juste la
				// grille colorée et la valeur de la cellule survolée.
				DrawHeatmap(ren, plotRect, p.heatmap);
				if (p.hoveredSeries >= 0 && size_t(p.hoveredSeries) < p.heatmap.values.size()) {
					String valText = String::From(p.heatmap.values[size_t(p.hoveredSeries)], 2);
					float fs = rs.FontSize(12.f);
					sdl3::FPoint tsz = MeasureCached(valText, fs);
					float tx = sdl3::Clamp(s.x + s.w * 0.5f - tsz.x * 0.5f, s.x, sdl3::Max(s.x, s.x + s.w - tsz.x));
					DrawTextRaw(ren, valText, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), tx, s.y + 2.f, tsz.y);
				}
				ren.ClearClipRect();
				if (!p.title.IsEmpty())
					DrawTextCentered(ren, e, p.title, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), sdl3::FRect{s.x, s.y + 2.f, s.w, 16.f},
									 TextAlign::Center);
				return;
			}

			if (p.mode == PlotMode::CANDLE) {
				// ── PlotMode::CANDLE (Phase 6) : graduations/grille dans le
				// même style que XY, mais DUPLIQUÉES plutôt que partagées — les
				// deux blocs sont structurellement proches mais pas identiques
				// (pas de légende, pas d'axe Y secondaire, axes TOUJOURS résolus
				// depuis candleBars via resolveCandleAxis() plutôt que
				// resolveAxis()) ; les entremêler risquerait plus de bugs que la
				// duplication n'en évite (même raisonnement déjà appliqué à
				// detail::appendThickSegment entre plot.hpp et nodegraph.hpp).
				PlotAxis xr = ResolveCandleAxis(p.xAxis, p.candleBars, true);
				PlotAxis yr = ResolveCandleAxis(p.yAxis, p.candleBars, false);
				DrawPlotGrid(ren, plotRect, xr, yr, sdl3::FColor::UI_PANEL_DARK2());
				DrawCandleChart(ren, plotRect, p.candleBars, xr, yr, p.candleWidth, p.candleBullColor,
								p.candleBearColor, p.hoveredSeries);

				{
					bool top = p.xAxis.tickPosition == PlotEdge::Top;
					float ly = p.xAxis.tickOverlay ? (top ? plotRect.y + 2.f : plotRect.y + plotRect.h - tickFs - 2.f)
												   : (top ? plotRect.y - K_PLOT_TICK_MARGIN + 2.f
														  : plotRect.y + plotRect.h + 4.f);
					auto ticks = GenerateTicks(xr.min, xr.max);
					int dec = ticks.size() >= 2 ? TickDecimals(ticks[1] - ticks[0]) : 1;
					if (p.xAxis.tickOverlay && !ticks.empty()) {
						ren.SetDrawColor(p.xAxis.tickBoxColor);
						ren.FillRect({plotRect.x, ly - 1.f, plotRect.w, tickFs + 2.f});
					}
					for (float t : ticks) {
						float sx = DataToScreen(xr, plotRect.x, plotRect.x + plotRect.w, t);
						String label = String::From(t, dec);
						sdl3::FPoint tsz = MeasureCached(label, tickFs);
						DrawTextRaw(ren, label, tickTextColor, sx - tsz.x * 0.5f, ly, tickFs);
					}
				}
				{
					bool right = p.yAxis.tickPosition == PlotEdge::Right;
					auto ticks = GenerateTicks(yr.min, yr.max);
					int dec = ticks.size() >= 2 ? TickDecimals(ticks[1] - ticks[0]) : 1;
					float boxW = K_PLOT_TICK_MARGIN - 2.f;
					if (p.yAxis.tickOverlay && !ticks.empty()) {
						float bx = right ? plotRect.x + plotRect.w - boxW : plotRect.x;
						ren.SetDrawColor(p.yAxis.tickBoxColor);
						ren.FillRect({bx, plotRect.y, boxW, plotRect.h});
					}
					for (float t : ticks) {
						float sy = DataToScreen(yr, plotRect.y + plotRect.h, plotRect.y, t);
						String label = String::From(t, dec);
						sdl3::FPoint tsz = MeasureCached(label, tickFs);
						float lx = p.yAxis.tickOverlay
									  ? (right ? plotRect.x + plotRect.w - tsz.x - 3.f : plotRect.x + 3.f)
									  : (right ? plotRect.x + plotRect.w + 3.f : plotRect.x - tsz.x - 3.f);
						DrawTextRaw(ren, label, tickTextColor, lx, sy - tsz.y * 0.5f, tsz.y);
					}
				}

				// ── Étiquette de la bougie survolée (OHLC) ────────────────────
				if (p.hoveredSeries >= 0 && size_t(p.hoveredSeries) < p.candleBars.size()) {
					const OhlcBar &hb = p.candleBars[size_t(p.hoveredSeries)];
					String full;
					full.Append("O:");
					full.Append(String::From(hb.open, 2));
					full.Append(" H:");
					full.Append(String::From(hb.high, 2));
					full.Append(" L:");
					full.Append(String::From(hb.low, 2));
					full.Append(" C:");
					full.Append(String::From(hb.close, 2));
					float fs = rs.FontSize(12.f);
					sdl3::FPoint tsz = MeasureCached(full, fs);
					float tx = sdl3::Clamp(s.x + s.w * 0.5f - tsz.x * 0.5f, s.x, sdl3::Max(s.x, s.x + s.w - tsz.x));
					DrawTextRaw(ren, full, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), tx, s.y + 2.f, tsz.y);
				}

				ren.ClearClipRect();
				if (!p.title.IsEmpty())
					DrawTextCentered(ren, e, p.title, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), sdl3::FRect{s.x, s.y + 2.f, s.w, 16.f},
									 TextAlign::Center);
				return;
			}

			PlotAxis xr = ResolveAxis(p.xAxis, p.series, true);
			PlotAxis yr = ResolveAxis(p.yAxis, p.series, false);
			bool hasY2 = PlotHasSecondaryY(p);
			PlotAxis yr2 = hasY2 ? ResolveAxis(p.yAxis2, p.series, false, true) : PlotAxis{};

			// Pas de grille pour yAxis2 (Phase 4) — convention courante (ImPlot
			// compris) : une seconde grille superposée à la première brouille
			// plus qu'elle n'aide, l'axe secondaire garde ses graduations mais
			// pas ses propres lignes.
			DrawPlotGrid(ren, plotRect, xr, yr, sdl3::FColor::UI_PANEL_DARK2());

			// Séries : estompées si une AUTRE entrée de légende est survolée
			// (cf. UiPlot::legendHover, Phase 2) ; chacune choisit yAxis ou
			// yAxis2 selon PlotSeries::useSecondaryY (Phase 4).
			for (size_t i = 0; i < p.series.size(); ++i) {
				float dim = (p.legendHover < 0 || int(i) == p.legendHover) ? 1.f : 0.25f;
				const PlotAxis &yForSeries = p.series[i].useSecondaryY ? yr2 : yr;
				DrawPlotSeries(ren, plotRect, p.series[i], xr, yForSeries, dim);
			}

			// ── Graduations X (bas/haut, réservées ou superposées) ──────────
			// Les labels restent dessinés même si `showGrid` masque les
			// lignes de grille elles-mêmes — deux réglages indépendants.
			{
				bool top = p.xAxis.tickPosition == PlotEdge::Top;
				float ly = p.xAxis.tickOverlay ? (top ? plotRect.y + 2.f : plotRect.y + plotRect.h - tickFs - 2.f)
											   : (top ? plotRect.y - K_PLOT_TICK_MARGIN + 2.f : plotRect.y + plotRect.h + 4.f);
				auto ticks = xr.logScale ? GenerateLogTicks(xr.min, xr.max) : GenerateTicks(xr.min, xr.max);
				int dec = ticks.size() >= 2 ? TickDecimals(ticks[1] - ticks[0]) : 1;
				if (p.xAxis.tickOverlay && !ticks.empty()) {
					ren.SetDrawColor(p.xAxis.tickBoxColor);
					ren.FillRect({plotRect.x, ly - 1.f, plotRect.w, tickFs + 2.f});
				}
				for (float t : ticks) {
					float sx = DataToScreen(xr, plotRect.x, plotRect.x + plotRect.w, t);
					String label = String::From(t, dec);
					sdl3::FPoint tsz = MeasureCached(label, tickFs);
					DrawTextRaw(ren, label, tickTextColor, sx - tsz.x * 0.5f, ly, tickFs);
				}
			}
			// ── Graduations Y (gauche/droite, réservées ou superposées) ─────
			{
				bool right = p.yAxis.tickPosition == PlotEdge::Right;
				auto ticks = yr.logScale ? GenerateLogTicks(yr.min, yr.max) : GenerateTicks(yr.min, yr.max);
				int dec = ticks.size() >= 2 ? TickDecimals(ticks[1] - ticks[0]) : 1;
				float boxW = K_PLOT_TICK_MARGIN - 2.f;
				if (p.yAxis.tickOverlay && !ticks.empty()) {
					float bx = right ? plotRect.x + plotRect.w - boxW : plotRect.x;
					ren.SetDrawColor(p.yAxis.tickBoxColor);
					ren.FillRect({bx, plotRect.y, boxW, plotRect.h});
				}
				for (float t : ticks) {
					float sy = DataToScreen(yr, plotRect.y + plotRect.h, plotRect.y, t);
					String label = String::From(t, dec);
					sdl3::FPoint tsz = MeasureCached(label, tickFs);
					float lx = p.yAxis.tickOverlay
								  ? (right ? plotRect.x + plotRect.w - tsz.x - 3.f : plotRect.x + 3.f)
								  : (right ? plotRect.x + plotRect.w + 3.f : plotRect.x - tsz.x - 3.f);
					DrawTextRaw(ren, label, tickTextColor, lx, sy - tsz.y * 0.5f, tsz.y);
				}
			}
			// ── Graduations Y secondaires (Phase 4) — même logique que
			// l'axe Y primaire ci-dessus, actif seulement si au moins une
			// série a `useSecondaryY=true` ; pas de grille dédiée (cf. plus
			// haut), seulement les graduations.
			if (hasY2) {
				bool right = p.yAxis2.tickPosition == PlotEdge::Right;
				auto ticks = yr2.logScale ? GenerateLogTicks(yr2.min, yr2.max) : GenerateTicks(yr2.min, yr2.max);
				int dec = ticks.size() >= 2 ? TickDecimals(ticks[1] - ticks[0]) : 1;
				float boxW = K_PLOT_TICK_MARGIN - 2.f;
				if (p.yAxis2.tickOverlay && !ticks.empty()) {
					float bx = right ? plotRect.x + plotRect.w - boxW : plotRect.x;
					ren.SetDrawColor(p.yAxis2.tickBoxColor);
					ren.FillRect({bx, plotRect.y, boxW, plotRect.h});
				}
				for (float t : ticks) {
					float sy = DataToScreen(yr2, plotRect.y + plotRect.h, plotRect.y, t);
					String label = String::From(t, dec);
					sdl3::FPoint tsz = MeasureCached(label, tickFs);
					float lx = p.yAxis2.tickOverlay
								  ? (right ? plotRect.x + plotRect.w - tsz.x - 3.f : plotRect.x + 3.f)
								  : (right ? plotRect.x + plotRect.w + 3.f : plotRect.x - tsz.x - 3.f);
					DrawTextRaw(ren, label, tickTextColor, lx, sy - tsz.y * 0.5f, tsz.y);
				}
			}

			// ── Curseur/infobulle (point survolé) ────────────────────────────
			if (p.hoveredSeries >= 0 && size_t(p.hoveredSeries) < p.series.size()) {
				const PlotSeries &hs = p.series[size_t(p.hoveredSeries)];
				if (size_t(p.hoveredIndex) < hs.x.size()) {
					float hx = DataToScreen(xr, plotRect.x, plotRect.x + plotRect.w, hs.x[size_t(p.hoveredIndex)]);
					ren.SetDrawColor(sdl3::FColor{220/255.f, 222/255.f, 232/255.f, 100/255.f});
					ren.DrawLine(hx, plotRect.y, hx, plotRect.y + plotRect.h);
					String valText = String::From(hs.y[size_t(p.hoveredIndex)], 2);
					float fs = rs.FontSize(12.f);
					sdl3::FPoint tsz = MeasureCached(valText, fs);
					float tx = sdl3::Clamp(hx - tsz.x * 0.5f, s.x, sdl3::Max(s.x, s.x + s.w - tsz.x));
					DrawTextRaw(ren, valText, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), tx, s.y + 2.f, tsz.y);
				}
			}

			// ── Légende (fond+pastilles via plot.hpp, texte ici) ─────────────
			if (p.showLegend && !p.series.empty()) {
				sdl3::FRect legendBox = LegendBoxRect(p, s, plotRect);
				DrawPlotLegendBackground(ren, p, legendBox);
				float legendFs = rs.FontSize(11.f);
				for (size_t i = 0; i < p.series.size(); ++i) {
					if (p.series[i].label.IsEmpty())
						continue;
					sdl3::FRect labelRect = LegendLabelRect(LegendRowRect(legendBox, i));
					sdl3::FColor lc = tickTextColor;
					if (!p.series[i].visible)
						lc.a = lc.a * 0.5f;
					sdl3::FPoint tsz = MeasureCached(p.series[i].label, legendFs);
					DrawTextRaw(ren, p.series[i].label, lc, labelRect.x, labelRect.y + (labelRect.h - tsz.y) * 0.5f,
							   legendFs);
				}
			}

			// ── Rectangle de zoom-rectangle en cours (Phase 3, clic-droit
			// glissé) ────────────────────────────────────────────────────
			DrawPlotBoxZoomRect(ren, p);

			ren.ClearClipRect();

			if (!p.title.IsEmpty())
				DrawTextCentered(ren, e, p.title, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), sdl3::FRect{s.x, s.y + 2.f, s.w, 16.f},
								 TextAlign::Center);
		}

		if (auto rh = world.GetComponent<UiResizeHandle>(e); rh.IsSome()) {
			// Rendu minimal (barre pleine, teinte au survol/glisser) — cf.
			// interaction.hpp : UiResizeHandle ne dessine RIEN par lui-même
			// en tant que primitive transverse (Phase 2), mais un Splitter
			// (Phase 8) a besoin d'une poignée VISIBLE, contrairement aux
			// colonnes de Table (Phase 6) qui redessinent leur propre bordure.
			const UiResizeHandle &h = *rh.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FColor bg = (h.dragging || h.hovered) ? rs.BgHovered(sdl3::FColor::UI_ACCENT_BLUE_BRIGHT()) : rs.Bg(sdl3::FColor::UI_BORDER_MUTED());
			ren.SetDrawColor(bg);
			ren.FillRoundedRect(s, rs.BordersRadius(math::Corners(2.f)));
		}

		if (auto calo = world.GetComponent<UiCalendar>(e); calo.IsSome()) {
			const UiCalendar &cal = *calo.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_BG_DEEP()));
			ren.FillRoundedRect(s, math::Corners(4.f));
			ren.SetDrawColor(rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
			ren.DrawRoundedRect(s, math::Corners(4.f));

			const float HEADER_H = 28.f;
			sdl3::FRect header{s.x, s.y, s.w, HEADER_H};
			if (cal.hoveredPrevArrow || cal.hoveredNextArrow) {
				ren.SetDrawColor(rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()));
				ren.FillRect(cal.hoveredPrevArrow ? sdl3::FRect{s.x, s.y, 28.f, HEADER_H}
												  : sdl3::FRect{s.x + s.w - 28.f, s.y, 28.f, HEADER_H});
			}
			float pcx = s.x + 14.f, pcy = s.y + HEADER_H * 0.5f;
			std::array<sdl3::Vertex, 3> prevTri{{
				{{pcx + 4.f, pcy - 5.f}, textColor, {}},
				{{pcx + 4.f, pcy + 5.f}, textColor, {}},
				{{pcx - 4.f, pcy}, textColor, {}},
			}};
			ren.RenderGeometry(prevTri);
			float ncx = s.x + s.w - 14.f, ncy = pcy;
			std::array<sdl3::Vertex, 3> nextTri{{
				{{ncx - 4.f, ncy - 5.f}, textColor, {}},
				{{ncx - 4.f, ncy + 5.f}, textColor, {}},
				{{ncx + 4.f, ncy}, textColor, {}},
			}};
			ren.RenderGeometry(nextTri);
			String title = String::Format("%s %d", MonthNameFr(cal.month), cal.year);
			DrawTextCentered(ren, e, title, textColor, header, TextAlign::Center);

			sdl3::FRect grid{s.x, s.y + HEADER_H, s.w, sdl3::Max(0.f, s.h - HEADER_H)};
			float cellW = grid.w / 7.f, cellH = grid.h / 6.f;
			int offset = FirstWeekdayOfMonth(cal.year, cal.month);
			int dim = DaysInMonth(cal.year, cal.month);
			float fs = rs.FontSize(13.f);
			for (int day = 1; day <= dim; ++day) {
				int idx = offset + day - 1;
				int row = idx / 7, col = idx % 7;
				sdl3::FRect cell{grid.x + float(col) * cellW, grid.y + float(row) * cellH, cellW, cellH};
				if (day == cal.selectedDay) {
					ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_DEEP()));
					ren.FillRoundedRect({cell.x + 2.f, cell.y + 2.f, cell.w - 4.f, cell.h - 4.f}, math::Corners(3.f));
				} else if (day == cal.hoveredDay) {
					ren.SetDrawColor(rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()));
					ren.FillRoundedRect({cell.x + 2.f, cell.y + 2.f, cell.w - 4.f, cell.h - 4.f}, math::Corners(3.f));
				}
				String dayText = String::From(day);
				sdl3::FPoint tsz = MeasureCached(dayText, fs);
				DrawTextRaw(ren, dayText, textColor, cell.x + (cell.w - tsz.x) * 0.5f, cell.y + (cell.h - tsz.y) * 0.5f,
						   tsz.y);
			}
		}

		if (auto selp = world.GetComponent<UiSelectable>(e); selp.IsSome()) {
			const UiSelectable &sl = *selp.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			// Même correctif que pour UiMenuBarItem ci-dessus : au repos, fond
			// RÉSOLU (thémé) et non un noir opaque codé en dur.
			sdl3::FColor bg = sl.selected
								  ? rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_GLOW())
								  : (sl.hovered ? rs.BgHovered(sdl3::FColor::UI_HIGHLIGHT_FAINT())
												: rs.Bg(sdl3::FColor{0.f, 0.f, 0.f, 0.f}));
			if (bg.a > 0) {
				ren.SetDrawColor(bg);
				ren.FillRoundedRect(s, rs.BordersRadius(math::Corners(4.f)));
			}
		}

		if (auto tnp = world.GetComponent<UiTreeNode>(e); tnp.IsSome()) {
			const UiTreeNode &tn = *tnp.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FRect header{s.x, s.y, s.w, tn.headerHeight};
			sdl3::FColor bg = tn.selected
								  ? rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_GLOW())
								  : (tn.hovered ? rs.BgHovered(sdl3::FColor::UI_HIGHLIGHT_FAINT())
												: rs.Bg(sdl3::FColor{0.f, 0.f, 0.f, 0.f}));
			if (bg.a > 0) {
				ren.SetDrawColor(bg);
				ren.FillRoundedRect(header, rs.BordersRadius(math::Corners(4.f)));
			}
			bool hasKids = false;
			if (auto ch = world.GetComponent<UiChildren>(e); ch.IsSome())
				hasKids = !ch.Unwrap()->list.empty();
			if (hasKids) {
				sdl3::FColor arrowColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
				float cx = s.x + 10.f, cy = s.y + tn.headerHeight * 0.5f;
				if (tn.expanded) {
					std::array<sdl3::Vertex, 3> tri{{
						{{cx - 5.f, cy - 4.f}, arrowColor, {}},
						{{cx + 5.f, cy - 4.f}, arrowColor, {}},
						{{cx, cy + 4.f}, arrowColor, {}},
					}};
					ren.RenderGeometry(tri);
				} else {
					std::array<sdl3::Vertex, 3> tri{{
						{{cx - 3.f, cy - 5.f}, arrowColor, {}},
						{{cx - 3.f, cy + 5.f}, arrowColor, {}},
						{{cx + 5.f, cy}, arrowColor, {}},
					}};
					ren.RenderGeometry(tri);
				}
			}
			sdl3::FRect titleBox{s.x + 20.f, s.y, sdl3::Max(0.f, s.w - 20.f), tn.headerHeight};
			DrawTextCentered(ren, e, tn.title, rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()), titleBox, TextAlign::Left);
		}

		if (auto rd = world.GetComponent<UiRadio>(e); rd.IsSome()) {
			const UiRadio &r = *rd.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			float d = sdl3::Min(18.f, s.h); // diamètre du cercle
			sdl3::FPoint center{s.x + d * 0.5f, s.y + s.h * 0.5f};
			ren.SetDrawColor(r.hovered ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
			ren.FillCircle(center, d * 0.5f);
			ren.SetDrawColor(textColor);
			ren.DrawCircle(center, d * 0.5f);
			if (r.checked) {
				ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE()));
				ren.FillCircle(center, d * 0.28f);
			}
			if (!r.text.IsEmpty()) {
				sdl3::FRect labelBox{s.x + d + 6.f, s.y, sdl3::Max(0.f, s.w - d - 6.f), s.h};
				DrawTextCentered(ren, e, r.text, textColor, labelBox, TextAlign::Left);
			}
		}

		if (auto sb = world.GetComponent<UiScrollBar>(e); sb.IsSome()) {
			const UiScrollBar &b2 = *sb.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			float radius = sdl3::Min(s.w, s.h) * 0.5f;
			ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
			ren.FillRoundedRect(s, math::Corners(radius));
			bool horiz = b2.orient == Orientation::Horizontal;
			float trackLen = horiz ? s.w : s.h;
			float thumbLen = trackLen * b2.ThumbRatio();
			float thumbPos = (trackLen - thumbLen) * b2.Normalized();
			sdl3::FRect thumb = horiz ? sdl3::FRect{s.x + thumbPos, s.y, thumbLen, s.h} : sdl3::FRect{s.x, s.y + thumbPos, s.w, thumbLen};
			sdl3::FColor thumbColor = b2.dragging ? rs.BgPressed(sdl3::FColor::UI_ACCENT_BLUE())
											: (b2.hovered ? rs.BgHovered(sdl3::FColor::UI_ACCENT_BLUE_LIGHT())
														  : rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_LIGHT()));
			ren.SetDrawColor(thumbColor);
			ren.FillRoundedRect(thumb, math::Corners(radius));
		}

		if (auto kn = world.GetComponent<UiKnob>(e); kn.IsSome()) {
			const UiKnob &k = *kn.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FPoint center{s.x + s.w * 0.5f, s.y + s.h * 0.5f};
			float radius = sdl3::Min(s.w, s.h) * 0.5f - 2.f;
			ren.SetDrawColor(k.hovered || k.dragging ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
			ren.FillCircle(center, radius);
			ren.SetDrawColor(rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY()));
			ren.DrawCircle(center, radius);
			// Balayage standard de potentiomètre : 135° (bas-gauche) → 405°.
			float ang = sdl3::DegToRad(135.f + k.value * 270.f);
			ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE()));
			sdl3::FPoint tip{center.x + sdl3::Cos(ang) * radius * 0.75f, center.y + sdl3::Sin(ang) * radius * 0.75f};
			ren.DrawLine(center, tip);
			ren.FillCircle(tip, 2.5f);
		}

		if (auto cv = world.GetComponent<UiCanvas>(e); cv.IsSome()) {
			if (cv.Unwrap()->onDraw)
				if (auto *nr = ren.NativeRenderer())
					cv.Unwrap()->onDraw(*nr, s);
		}

		if (auto cbo = world.GetComponent<UiComboBox>(e); cbo.IsSome()) {
			const UiComboBox &cb = *cbo.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			ren.SetDrawColor(cb.hovered && !cb.open ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : rs.Bg(sdl3::FColor::UI_PANEL_DARKER()));
			ren.FillRoundedRect(s, math::Corners(4.f));
			ren.SetDrawColor(cb.open ? rs.BorderFocus(sdl3::FColor::UI_ACCENT_BLUE_HOVER()) : rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
			ren.DrawRoundedRect(s, math::Corners(4.f));
			if (cb.selected >= 0 && cb.selected < int(cb.items.size())) {
				sdl3::FRect textBox{s.x, s.y, sdl3::Max(0.f, s.w - 24.f), s.h};
				DrawTextCentered(ren, e, cb.items[size_t(cb.selected)], textColor, textBox, TextAlign::Left);
			}
			// Flèche ▼ (ou ▲ liste ouverte) à droite.
			float axc = s.x + s.w - 14.f, ayc = s.y + s.h * 0.5f;
			ren.SetDrawColor(textColor);
			if (cb.open) {
				ren.DrawLine(axc - 5.f, ayc + 2.5f, axc, ayc - 2.5f);
				ren.DrawLine(axc, ayc - 2.5f, axc + 5.f, ayc + 2.5f);
			} else {
				ren.DrawLine(axc - 5.f, ayc - 2.5f, axc, ayc + 2.5f);
				ren.DrawLine(axc, ayc + 2.5f, axc + 5.f, ayc - 2.5f);
			}
		}

		if (auto lbo = world.GetComponent<ListBox>(e); lbo.IsSome()) {
			const ListBox &lb = *lbo.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_BG_DEEP()));
			ren.FillRoundedRect(s, math::Corners(4.f));
			ren.SetDrawColor(rs.BorderColor(sdl3::FColor::UI_BORDER_MUTED()));
			ren.DrawRoundedRect(s, math::Corners(4.f));

			// Clip resserré au rect de la liste pour les items.
			sdl3::FRect inner = c.clip.Intersection(s);
			if (inner.w > 0.f && inner.h > 0.f && lb.itemHeight > 0.f) {
				ren.SetClipRect(sdl3::Rect{int(inner.x), int(inner.y), int(inner.w) + 1, int(inner.h) + 1});
				int first = sdl3::Max(0, int(lb.scroll / lb.itemHeight));
				int last = sdl3::Min(int(lb.items.size()) - 1, int((lb.scroll + s.h) / lb.itemHeight));
				for (int i = first; i <= last; ++i) {
					sdl3::FRect row{s.x, s.y + float(i) * lb.itemHeight - lb.scroll, s.w, lb.itemHeight};
					if (i == lb.selected) {
						ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_DEEP()));
						ren.FillRect(row);
					} else if (i == lb.hoveredItem) {
						ren.SetDrawColor(rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()));
						ren.FillRect(row);
					}
					DrawTextRaw(ren, lb.items[size_t(i)], textColor, row.x + 8.f, row.y, row.h);
				}
				// Barre de défilement inline à droite si débordement.
				float maxSc = lb.MaxScroll(s.h);
				if (maxSc > 0.f) {
					float ratio = sdl3::Clamp(s.h / lb.ContentHeight(), 0.05f, 1.f);
					float thumbH = s.h * ratio;
					float thumbY = s.y + (s.h - thumbH) * (lb.scroll / maxSc);
					ren.SetDrawColor(rs.BgPressed(sdl3::FColor::UI_ACCENT_BLUE_LIGHT()));
					ren.FillRoundedRect({s.x + s.w - 6.f, thumbY, 4.f, thumbH}, math::Corners(2.f));
				}
				ren.SetClipRect(sdl3::Rect{int(c.clip.x), int(c.clip.y), int(c.clip.w) + 1, int(c.clip.h) + 1});
			}
		}

		if (auto xo = world.GetComponent<UiExpander>(e); xo.IsSome()) {
			const UiExpander &x = *xo.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			sdl3::FRect header{s.x, s.y, s.w, x.headerHeight};
			ren.SetDrawColor(x.hovered ? rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()) : rs.Bg(sdl3::FColor::UI_PANEL_DARK()));
			ren.FillRoundedRect(header, math::Corners(4.f));
			// Flèche ▶ (replié) / ▼ (déplié).
			float acx = s.x + 12.f, acy = s.y + x.headerHeight * 0.5f;
			ren.SetDrawColor(textColor);
			if (x.expanded) {
				ren.DrawLine(acx - 4.f, acy - 2.f, acx, acy + 3.f);
				ren.DrawLine(acx, acy + 3.f, acx + 4.f, acy - 2.f);
			} else {
				ren.DrawLine(acx - 2.f, acy - 4.f, acx + 3.f, acy);
				ren.DrawLine(acx + 3.f, acy, acx - 2.f, acy + 4.f);
			}
			sdl3::FRect titleBox{s.x + 22.f, s.y, sdl3::Max(0.f, s.w - 22.f), x.headerHeight};
			DrawTextCentered(ren, e, x.title, textColor, titleBox, TextAlign::Left);
		}

		if (auto tvo = world.GetComponent<UiTabView>(e); tvo.IsSome()) {
			const UiTabView &tv = *tvo.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
			float fs = rs.FontSize(14.f);
			if (!tv.tabs.empty()) {
				sdl3::FRect bar{s.x, s.y, s.w, tv.tabHeight};
				ren.SetDrawColor(rs.Bg(sdl3::FColor{24/255.f, 26/255.f, 36/255.f, 255/255.f}));
				ren.FillRect(bar);
				float tabW = s.w / float(tv.tabs.size());
				for (size_t i = 0; i < tv.tabs.size(); ++i) {
					sdl3::FRect tab{s.x + float(i) * tabW, s.y, tabW, tv.tabHeight};
					if (int(i) == tv.active) {
						ren.SetDrawColor(rs.BgChecked(sdl3::FColor{45/255.f, 52/255.f, 76/255.f, 255/255.f}));
						ren.FillRect(tab);
					} else if (int(i) == tv.hoveredTab) {
						ren.SetDrawColor(rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()));
						ren.FillRect(tab);
					}
					sdl3::FPoint tsz = MeasureCached(tv.tabs[i], fs);
					DrawTextRaw(ren, tv.tabs[i], textColor, tab.x + (tab.w - tsz.x) * 0.5f, tab.y, tab.h);
				}
				// Soulignement de l'onglet actif.
				sdl3::FRect underline{s.x + float(tv.active) * tabW, s.y + tv.tabHeight - 2.f, tabW, 2.f};
				ren.SetDrawColor(rs.BorderFocus(sdl3::FColor::UI_ACCENT_BLUE_BRIGHT()));
				ren.FillRect(underline);
			}
		}

		if (auto spo = world.GetComponent<UiSpinner>(e); spo.IsSome()) {
			const UiSpinner &sp = *spo.Unwrap();
			sdl3::FPoint center{s.x + s.w * 0.5f, s.y + s.h * 0.5f};
			float radius = sdl3::Min(s.w, s.h) * 0.5f - 2.f;
			ren.SetDrawColor(GetResolved(world, e).Bg(sdl3::FColor::UI_ACCENT_BLUE_BRIGHT()));
			// Arc de 300° tournant — épaissi par cercles concentriques.
			int th = sdl3::Max(1, int(sp.thickness));
			for (int i = 0; i < th; ++i)
				ren.DrawArc(center, radius - float(i), sp.angle, sp.angle + 300.f);
		}

		if (auto bdo = world.GetComponent<UiBadge>(e); bdo.IsSome()) {
			const UiBadge &bd = *bdo.Unwrap();
			ResolvedStyle rs = GetResolved(world, e);
			ren.SetDrawColor(rs.Bg(sdl3::FColor{200/255.f, 60/255.f, 50/255.f, 255/255.f}));
			ren.FillRoundedRect(s, math::Corners(s.h * 0.5f));
			DrawTextCentered(ren, e, bd.text, rs.TextColor(sdl3::FColor::WHITE()), s, TextAlign::Center);
		}
	}

	/// Liste déroulée d'un combo — en overlay, hors de tout clip.
	/// Piste + pouce sur les bords droit/bas de `screen`, un par axe en
	/// débordement (cf. vScrollbarThumbRect/hScrollbarThumbRect, la même
	/// géométrie que celle utilisée par InputSystem pour le drag).
	void DrawScrollbars(IUiRenderBackend &ren, const UiRect &r, const sdl3::FRect &screen) {
		if (sdl3::FRect thumb = VScrollbarThumbRect(r, screen); thumb.w > 0.f) {
			sdl3::FRect track{screen.x + screen.w - K_SCROLLBAR_THICKNESS, screen.y, K_SCROLLBAR_THICKNESS, screen.h};
			ren.SetDrawColor(scrollbarTrack);
			ren.FillRect(track);
			ren.SetDrawColor(scrollbarThumb);
			ren.FillRoundedRect(thumb, math::Corners(K_SCROLLBAR_THICKNESS * 0.5f));
		}
		if (sdl3::FRect thumb = HScrollbarThumbRect(r, screen); thumb.w > 0.f) {
			sdl3::FRect track{screen.x, screen.y + screen.h - K_SCROLLBAR_THICKNESS, screen.w, K_SCROLLBAR_THICKNESS};
			ren.SetDrawColor(scrollbarTrack);
			ren.FillRect(track);
			ren.SetDrawColor(scrollbarThumb);
			ren.FillRoundedRect(thumb, math::Corners(K_SCROLLBAR_THICKNESS * 0.5f));
		}
	}

	void DrawDropdown(IUiRenderBackend &ren, const UiComboBox &cb, const sdl3::FRect &box, const ResolvedStyle &rs) {
		ren.ClearClipRect();
		sdl3::FRect dd = cb.DropdownRect(box);
		sdl3::FColor textColor = rs.TextColor(sdl3::FColor::UI_TEXT_PRIMARY());
		ren.SetDrawColor(rs.Bg(sdl3::FColor::UI_PANEL_DARKER()));
		ren.FillRoundedRect(dd, math::Corners(4.f));
		for (size_t i = 0; i < cb.items.size(); ++i) {
			sdl3::FRect row{dd.x, dd.y + float(i) * cb.itemHeight, dd.w, cb.itemHeight};
			if (int(i) == cb.selected) {
				ren.SetDrawColor(rs.BgChecked(sdl3::FColor::UI_ACCENT_BLUE_DEEP()));
				ren.FillRect(row);
			} else if (int(i) == cb.hoveredItem) {
				ren.SetDrawColor(rs.BgHovered(sdl3::FColor::UI_BORDER_SLATE()));
				ren.FillRect(row);
			}
			DrawTextRaw(ren, cb.items[i], textColor, row.x + 8.f, row.y, row.h);
		}
		ren.SetDrawColor(rs.BorderFocus(sdl3::FColor::UI_ACCENT_BLUE_HOVER()));
		ren.DrawRoundedRect(dd, math::Corners(4.f));
	}

	void DrawTooltip(IUiRenderBackend &ren, const InputSystem::Tooltip &tip) {
		ren.ClearClipRect();
		sdl3::FPoint sz = MeasureCached(tip.text, 13.f);
		sdl3::FRect box{tip.pos.x, tip.pos.y, sz.x + 16.f, sz.y + 8.f};
		ren.SetDrawColor(tooltipBg);
		ren.FillRoundedRect(box, math::Corners(4.f));
		ren.SetDrawColor(tooltipBorder);
		ren.DrawRoundedRect(box, math::Corners(4.f));
		DrawTextRaw(ren, tip.text, tooltipText, box.x + 8.f, box.y, box.h);
	}

	/// Rend la texture dans `dst` selon le mode d'ajustement demandé.
	void DrawImageFit(IUiRenderBackend &ren, const sdl3::Texture &tex, const sdl3::FRect &dst, ImageFit fit) {
		sdl3::FPoint ts = tex.GetSize();
		if (ts.x <= 0.f || ts.y <= 0.f || dst.w <= 0.f || dst.h <= 0.f)
			return;
		switch (fit) {
		case ImageFit::FILL:
			ren.Render(tex, dst);
			break;
		case ImageFit::CONTAIN: {
			float k = sdl3::Min(dst.w / ts.x, dst.h / ts.y);
			sdl3::FRect d{dst.x + (dst.w - ts.x * k) * 0.5f, dst.y + (dst.h - ts.y * k) * 0.5f, ts.x * k, ts.y * k};
			ren.Render(tex, d);
			break;
		}
		case ImageFit::COVER: {
			// Rogne la source pour couvrir la destination sans déformer.
			float k = sdl3::Max(dst.w / ts.x, dst.h / ts.y);
			sdl3::FRect src{(ts.x - dst.w / k) * 0.5f, (ts.y - dst.h / k) * 0.5f, dst.w / k, dst.h / k};
			ren.Render(tex, src, dst);
			break;
		}
		case ImageFit::NONE: {
			// Taille d'origine, centrée, rognée à la destination.
			sdl3::FRect src{sdl3::Max(0.f, (ts.x - dst.w) * 0.5f), sdl3::Max(0.f, (ts.y - dst.h) * 0.5f),
					  sdl3::Min(ts.x, dst.w), sdl3::Min(ts.y, dst.h)};
			sdl3::FRect d{dst.x + sdl3::Max(0.f, (dst.w - ts.x) * 0.5f), dst.y + sdl3::Max(0.f, (dst.h - ts.y) * 0.5f), src.w,
					src.h};
			ren.Render(tex, src, d);
			break;
		}
		}
	}

	/// Sélectionne la police à utiliser pour du texte marqué bold/italic (cf.
	/// prop::Bold/prop::Italic, styles.hpp) — repli en cascade sur ce qui est
	/// effectivement enregistré (jamais d'absence de rendu) : bold+italic
	/// préfère la variante combinée, sinon gras seul, sinon italique seul,
	/// sinon la police normale.
	[[nodiscard]] sdl3::Font *PickTextFont(bool bold, bool italic) const {
		if (bold && italic && boldItalicFont)
			return boldItalicFont;
		if (bold && boldFont)
			return boldFont;
		if (italic && italicFont)
			return italicFont;
		return m_font;
	}

	/// Mesure une chaîne : vraie mesure TTF si possible, sinon heuristique.
	[[nodiscard]] sdl3::FPoint MeasureCached(const String &text, float fontSize) {
		if (m_font) {
			if (auto sz = m_font->Measure(text); sz.IsSome())
				return {float(sz.Unwrap().x), float(sz.Unwrap().y)};
		}
		return {float(text.size()) * fontSize * 0.55f, fontSize * 1.3f};
	}

	/// Texte via le cache clé-chaîne, centré verticalement dans `rowH`.
	void DrawTextRaw([[maybe_unused]] IUiRenderBackend &ren, const String &text, sdl3::FColor color, float x, float y, float rowH) {
		if (!m_engine || !m_font || text.IsEmpty())
			return;
		String key(text.c_str());
		auto it = strCache.find(key);
		if (it == strCache.end()) {
			auto res = sdl3::Text::Create(*m_engine, *m_font, text);
			if (!res)
				return;
			it = strCache.insert_or_assign(std::move(key), CachedStr{color, std::move(res.Value())}).first;
			it->second.text.SetColor(color);
		} else if (!(it->second.color == color)) {
			it->second.text.SetColor(color);
			it->second.color = color;
		}
		float th = rowH;
		if (auto s = it->second.text.GetSize(); s.IsSome())
			th = float(s.Unwrap().y);
		it->second.text.Draw(x, y + (rowH - th) * 0.5f);
	}

	/// Surbrillance de la sélection (rects semi-transparents, une bande par
	/// ligne couverte) pour UiInput/UiInputArea — dessinée AVANT le texte
	/// pour qu'il reste lisible par-dessus. Pas de sélection (cursor ==
	/// selectionAnchor) : ne dessine rien.
	void DrawSelection(IUiRenderBackend &ren, const std::vector<TextLine> &lines, float fontSize, size_t cursor,
					   size_t selectionAnchor, const sdl3::FRect &s, const sdl3::FPoint &scroll, sdl3::FColor accent) {
		if (cursor == selectionAnchor || lines.empty())
			return;
		size_t lo = sdl3::Min(cursor, selectionAnchor), hi = sdl3::Max(cursor, selectionAnchor);
		size_t loLi = LineIndexOf(lines, lo), hiLi = LineIndexOf(lines, hi);
		float lh = LineHeightApprox(fontSize);
		float cw = CharWidthApprox(fontSize);
		ren.SetDrawColor(sdl3::FColor{accent.r, accent.g, accent.b, 90});
		for (size_t li = loLi; li <= hiLi; ++li) {
			const TextLine &line = lines[li];
			size_t a = (li == loLi) ? (lo - line.offset) : 0;
			size_t b = (li == hiLi) ? (hi - line.offset) : line.text.size();
			float x0 = s.x + 8.f - scroll.x + float(a) * cw;
			float x1 = s.x + 8.f - scroll.x + float(b) * cw;
			float y = s.y + 4.f - scroll.y + float(li) * lh;
			if (y + lh >= s.y && y <= s.y + s.h)
				ren.FillRect({x0, y, sdl3::Max(1.f, x1 - x0), lh});
		}
	}

	/// Curseur clignotant à la position réelle de `cursor` (ligne + colonne),
	/// pas toujours en fin de texte — partagé entre UiInput et UiInputArea.
	void DrawCaret(IUiRenderBackend &ren, const std::vector<TextLine> &lines, float fontSize, size_t cursor,
				   const sdl3::FRect &s, const sdl3::FPoint &scroll, sdl3::FColor color) {
		if (lines.empty())
			return;
		size_t li = LineIndexOf(lines, cursor);
		size_t col = cursor - lines[li].offset;
		float lh = LineHeightApprox(fontSize);
		float x = s.x + 8.f - scroll.x + float(col) * CharWidthApprox(fontSize);
		float y = s.y + 4.f - scroll.y + float(li) * lh;
		ren.SetDrawColor(color);
		ren.FillRect({x, y, 2.f, lh});
	}

	void DrawVGradient(IUiRenderBackend &ren, const sdl3::FRect &r, sdl3::FColor top, sdl3::FColor bottom) {
		std::array<sdl3::Vertex, 6> v{{
			{{r.x, r.y}, top, {}},
			{{r.x + r.w, r.y}, top, {}},
			{{r.x, r.y + r.h}, bottom, {}},
			{{r.x + r.w, r.y}, top, {}},
			{{r.x + r.w, r.y + r.h}, bottom, {}},
			{{r.x, r.y + r.h}, bottom, {}},
		}};
		ren.RenderGeometry(v);
	}

	/// Dégradé horizontal 2 couleurs (cf. drawVGradient, même construction —
	/// gauche → droite au lieu de haut → bas). Utilisé par la glissière alpha
	/// (opaque → transparent) et le fond de secours des segments de teinte.
	void DrawHGradient(IUiRenderBackend &ren, const sdl3::FRect &r, sdl3::FColor left, sdl3::FColor right) {
		std::array<sdl3::Vertex, 6> v{{
			{{r.x, r.y}, left, {}},
			{{r.x + r.w, r.y}, right, {}},
			{{r.x, r.y + r.h}, left, {}},
			{{r.x + r.w, r.y}, right, {}},
			{{r.x + r.w, r.y + r.h}, right, {}},
			{{r.x, r.y + r.h}, left, {}},
		}};
		ren.RenderGeometry(v);
	}

	/// Dégradé bilinéaire 4 coins (interpolation des 4 couleurs sur le
	/// rectangle) — carré SV du sélecteur de couleur (cf. UiSVSquare) :
	/// blanc→teinte en haut, noir→noir en bas.
	void DrawQuadGradient(IUiRenderBackend &ren, const sdl3::FRect &r, sdl3::FColor tl, sdl3::FColor tr, sdl3::FColor bl, sdl3::FColor br) {
		std::array<sdl3::Vertex, 6> v{{
			{{r.x, r.y}, tl, {}},
			{{r.x + r.w, r.y}, tr, {}},
			{{r.x, r.y + r.h}, bl, {}},
			{{r.x + r.w, r.y}, tr, {}},
			{{r.x + r.w, r.y + r.h}, br, {}},
			{{r.x, r.y + r.h}, bl, {}},
		}};
		ren.RenderGeometry(v);
	}

	/// Piste arc-en-ciel de la glissière de teinte (cf. UiHueSlider) : 6
	/// segments égaux, un par transition de l'hexagone HSV (rouge→jaune→
	/// vert→cyan→bleu→magenta→rouge), chacun un dégradé horizontal 2 couleurs.
	void DrawHueBar(IUiRenderBackend &ren, const sdl3::FRect &s) {
		constexpr int K_STOPS = 6;
		float segW = s.w / float(K_STOPS);
		for (int i = 0; i < K_STOPS; ++i) {
			sdl3::FColor left = HsvToColor(float(i) * 60.f, 1.f, 1.f);
			sdl3::FColor right = HsvToColor(float(i + 1) * 60.f, 1.f, 1.f);
			sdl3::FRect seg{s.x + float(i) * segW, s.y, segW + 0.5f, s.h}; // +0.5 : pas d'interstice visible
			DrawHGradient(ren, seg, left, right);
		}
	}

	/// Damier gris clair/gris moyen — fond conventionnel de transparence
	/// (glissière alpha, pastille de couleur avec alpha < 255).
	void DrawCheckerboard(IUiRenderBackend &ren, const sdl3::FRect &s, const math::Corners &radius) {
		constexpr float K_CELL = 6.f;
		if (radius.tl > 0.f || radius.tr > 0.f || radius.bl > 0.f || radius.br > 0.f) {
			ren.SetDrawColor(sdl3::FColor{200/255.f, 200/255.f, 200/255.f, 255/255.f});
			ren.FillRoundedRect(s, radius);
		} else {
			ren.SetDrawColor(sdl3::FColor{200/255.f, 200/255.f, 200/255.f, 255/255.f});
			ren.FillRect(s);
		}
		ren.SetDrawColor(sdl3::FColor{150/255.f, 150/255.f, 150/255.f, 255/255.f});
		int cols = int(sdl3::Ceil(s.w / K_CELL)), rows = int(sdl3::Ceil(s.h / K_CELL));
		for (int row = 0; row < rows; ++row) {
			for (int col = 0; col < cols; ++col) {
				if ((row + col) % 2 != 0)
					continue;
				sdl3::FRect cell{s.x + float(col) * K_CELL, s.y + float(row) * K_CELL, K_CELL, K_CELL};
				cell = cell.Intersection(s);
				if (cell.w > 0.f && cell.h > 0.f)
					ren.FillRect(cell);
			}
		}
	}

	/// Bande de reflet translucide (blanc → transparent) sur le haut d'un
	/// rect, façon bouton/panneau « verre » Aero. Rectangulaire (ignore le
	/// radius du dessous, comme drawVGradient déjà pour UiPanel.gradient) —
	/// insetée pour rester visuellement crédible malgré les coins arrondis.
	void DrawGlossHighlight(IUiRenderBackend &ren, const sdl3::FRect &s, float radius, float strength) {
		if (strength <= 0.f)
			return;
		float inset = sdl3::Max(1.f, radius * 0.4f);
		sdl3::FRect band{s.x + inset, s.y + inset, s.w - 2.f * inset, s.h * 0.42f};
		if (band.w <= 0.f || band.h <= 0.f)
			return;
		sdl3::FColor top{1.0f, 1.0f, 1.0f, sdl3::Clamp(strength, 0.f, 1.f)};
		sdl3::FColor bottom{1.0f, 1.0f, 1.0f, 0};
		DrawVGradient(ren, band, top, bottom);
	}

	/// Anneaux de lueur (alpha décroissant) autour d'un rect — effet « glow »
	/// Aero. Dessinés hors du rect (outset croissant), donc visibles tant que
	/// le clip hérité du parent laisse la marge nécessaire.
	void DrawGlowRing(IUiRenderBackend &ren, const sdl3::FRect &s, float radius, float strength, sdl3::FColor glowColor) {
		if (strength <= 0.f)
			return;
		constexpr int K_RINGS = 3;
		for (int i = 0; i < K_RINGS; ++i) {
			float t = float(i) / float(K_RINGS);
			float alpha = strength * (1.f - t) * 255.f * 0.5f;
			if (alpha < 2.f)
				continue;
			float outset = 1.f + float(i) * 1.5f;
			sdl3::FRect r{s.x - outset, s.y - outset, s.w + 2.f * outset, s.h + 2.f * outset};
			ren.SetDrawColor(sdl3::FColor{glowColor.r, glowColor.g, glowColor.b, alpha / 255.f});
			ren.DrawRoundedRect(r, math::Corners(radius + outset));
		}
	}

	/// `font` : police à utiliser pour CE dessin (nullptr = police globale
	/// font — cas de tous les widgets textuels). UiIcon passe explicitement
	/// sa propre police d'icônes, résolue via iconFonts.
	void DrawTextCentered([[maybe_unused]] IUiRenderBackend &ren, ecs::Entity e, const String &text, sdl3::FColor color, const sdl3::FRect &box,
						  TextAlign align, sdl3::Font *font = nullptr) {
		sdl3::Font *f = font ? font : m_font;
		if (!m_engine || !f || text.IsEmpty())
			return;

		auto it = textCache.find(e.id);
		if (it == textCache.end() || it->second.str != text || it->second.font != f) {
			auto res = sdl3::Text::Create(*m_engine, *f, text);
			if (!res)
				return;
			it = textCache.insert_or_assign(e.id, CachedText{text, color, f, std::move(res.Value())}).first;
			it->second.text.SetColor(color);
		} else if (!(it->second.color == color)) {
			it->second.text.SetColor(color);
			it->second.color = color;
		}

		sdl3::FPoint sz{0.f, 0.f};
		if (auto s = it->second.text.GetSize(); s.IsSome())
			sz = {float(s.Unwrap().x), float(s.Unwrap().y)};

		float x = box.x + 8.f;
		if (align == TextAlign::Center)
			x = box.x + (box.w - sz.x) * 0.5f;
		else if (align == TextAlign::Right)
			x = box.x + box.w - sz.x - 8.f;
		float y = box.y + (box.h - sz.y) * 0.5f;
		it->second.text.Draw(x, y);
	}
};

} // namespace ui
