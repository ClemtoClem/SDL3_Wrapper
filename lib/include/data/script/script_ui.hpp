#pragma once
/**
 * data::script — l'interface utilisateur de la bibliothèque (ui::) vue depuis
 * le langage (« Script »), dans l'espace de noms `ui`.
 *
 * L'HÔTE décide où va l'interface du script (UiBinding) : typiquement un
 * calque posé sur la vue du jeu. Le script y construit ses widgets :
 *
 *   let menu = ui.column({anchor: "center", gap: 12, pad: 24, bg: "#101418e0", radius: 10})
 *   ui.label("MON JEU", {parent: menu, font_size: 36, bold: true})
 *   ui.button("Jouer", fn(b) { game.load_scene("Niveau 1") }, {parent: menu, w: 220})
 *   let barre = ui.progress(0, 1, {parent: menu, w: 300})
 *   barre.value = 0.5                     # (ou barre.set_value(0.5))
 *
 * Options communes (dernier argument, facultatif) : parent, name, w, h
 * (pixels, "auto", "grow", "50%"), grow, pad, gap, bg, color, border,
 * radius, font_size, bold, align (start|center|end|stretch), justify
 * (start|center|end|between), text_align (left|center|right), anchor
 * (top_left, top, …, center, …, bottom_right), offset [x, y], absolute,
 * fixed, hidden, pointer_through. Couleurs : "#rrggbb", "#rrggbbaa" ou
 * [r, g, b(, a)] (0–1, ou 0–255 si une composante dépasse 1).
 *
 * SANS ÉCRAN (hôte sans fenêtre, tests), les widgets restent des objets
 * ordinaires : texte, valeur et rappels fonctionnent, `w.click()` simule un
 * clic — le même script tourne donc en mode graphique et sans tête.
 *
 * Les rappels (clic, changement) déclenchés par l'utilisateur sont MIS EN
 * FILE puis exécutés par `UiBinding::Flush()` — que l'hôte appelle une fois
 * par image, hors de la boucle d'évènements : un rappel peut donc détruire
 * l'interface qui l'a appelé sans invalider ce que l'interface parcourait.
 * L'interface n'étant pas sûre entre fils, toutes ces fonctions s'exécutent
 * sur le fil principal (appelées d'un fil `async`, elles y sont renvoyées).
 */
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "../../ui/ui.hpp"
#include "script_std.hpp"

namespace data::script {

/// Ce que l'hôte fournit au script pour construire son interface.
struct UiBinding {
	ecs::ArchetypeRegistry *registry = nullptr;
	ui::UiFactory *factory = nullptr;
	ui::LayoutSystem *layout = nullptr;
	/// Conteneur des widgets du script ; entité invalide : pas d'écran pour
	/// l'instant (les widgets créés alors restent des objets sans affichage).
	std::function<ecs::Entity()> root;

	[[nodiscard]] static std::shared_ptr<UiBinding> FromUi(ui::Ui &gui, std::function<ecs::Entity()> rootProvider);

	[[nodiscard]] ecs::Entity Root() const;
	[[nodiscard]] bool Live() const { return Root().Valid(); }
	[[nodiscard]] bool Alive(ecs::Entity entity) const;
	void Relayout() const;

	/// Rappels du script en attente (cf. en-tête) ; exécutés par Flush().
	void Post(std::function<void()> callback);
	/// À appeler une fois par image, sur le fil principal, hors de la boucle
	/// d'évènements de l'interface. Rend le nombre de rappels exécutés.
	size_t Flush();

private:
	std::mutex m_mutex;
	std::vector<std::function<void()>> m_pending;
};

namespace uilib {

using lib::Args;
using lib::As;
using lib::Fail;
using lib::TypeBuilder;

/// Un widget du script : son entité (si l'écran existe) ET son état, qui
/// fait foi sans écran.
struct WidgetObject : HostObject {
	std::shared_ptr<UiBinding> binding;
	String kind;
	String name;
	ecs::Entity entity{};
	mutable std::mutex mutex;
	String text;
	String placeholder; ///< champ de saisie : indication affichée quand il est vide
	double value = 0.0, min = 0.0, max = 1.0;
	bool checked = false;
	bool visible = true;
	Value onClick, onChange;
	std::vector<std::weak_ptr<WidgetObject>> children;
};

struct UiState {
	std::shared_ptr<UiBinding> binding;
	std::shared_ptr<const HostType> widgetType;
	std::mutex mutex;
	std::vector<std::weak_ptr<WidgetObject>> named; ///< pour `ui.find(nom)`
	/// Widgets à l'écran, gardés en vie tant que leur entité existe : un
	/// script écrit souvent `ui.button("OK", fn(b) {…})` sans garder le
	/// résultat — sans cette référence, l'objet mourrait aussitôt et son
	/// rappel (qui ne le tient que faiblement) ne s'exécuterait jamais.
	std::vector<std::shared_ptr<WidgetObject>> onScreen;
};

[[nodiscard]] inline bool Live(const WidgetObject &w) { return w.binding && w.binding->Alive(w.entity); }

// ── Couleurs et options ─────────────────────────────────────────────────────

[[nodiscard]] Result<sdl3::FColor, ScriptError> ParseColor(const Value &v, const char *fn);

[[nodiscard]] Result<ui::Dimension, ScriptError> ParseDimension(const Value &v, const char *fn);

[[nodiscard]] Option<ui::Anchor> ParseAnchor(const String &s);

/// Constructeur factice : valide les options d'un widget créé SANS écran
/// (mêmes erreurs qu'avec écran, rien n'est construit).
struct NullBuilder {
	template <typename... A> NullBuilder &W(A &&...) { return *this; }
	template <typename... A> NullBuilder &H(A &&...) { return *this; }
	template <typename... A> NullBuilder &GrowW(A &&...) { return *this; }
	template <typename... A> NullBuilder &GrowH(A &&...) { return *this; }
	template <typename... A> NullBuilder &Pad(A &&...) { return *this; }
	template <typename... A> NullBuilder &Gap(A &&...) { return *this; }
	template <typename... A> NullBuilder &Bg(A &&...) { return *this; }
	template <typename... A> NullBuilder &TextColor(A &&...) { return *this; }
	template <typename... A> NullBuilder &BorderColor(A &&...) { return *this; }
	template <typename... A> NullBuilder &Radius(A &&...) { return *this; }
	template <typename... A> NullBuilder &FontSize(A &&...) { return *this; }
	template <typename... A> NullBuilder &Bold(A &&...) { return *this; }
	template <typename... A> NullBuilder &Italic(A &&...) { return *this; }
	template <typename... A> NullBuilder &Absolute(A &&...) { return *this; }
	template <typename... A> NullBuilder &Anchor(A &&...) { return *this; }
	template <typename... A> NullBuilder &Align(A &&...) { return *this; }
	template <typename... A> NullBuilder &Justify(A &&...) { return *this; }
	template <typename... A> NullBuilder &TextAlign(A &&...) { return *this; }
	template <typename... A> NullBuilder &Offset(A &&...) { return *this; }
	template <typename... A> NullBuilder &Fixed(A &&...) { return *this; }
	template <typename... A> NullBuilder &Hidden(A &&...) { return *this; }
	template <typename... A> NullBuilder &PointerThrough(A &&...) { return *this; }
	template <typename... A> NullBuilder &WAuto(A &&...) { return *this; }
	template <typename... A> NullBuilder &HAuto(A &&...) { return *this; }
};

/// Applique les options de mise en forme au constructeur de widget.
template <typename Builder>
[[nodiscard]] Option<ScriptError> ApplyOptions(Builder &b, const MapObject *opts, const char *fn) {
	bool hasW = false, hasH = false;
	for (const auto &[key, v] : opts ? opts->Snapshot() : std::vector<std::pair<String, Value>>{}) {
		hasW = hasW || key == "w" || key == "grow";
		hasH = hasH || key == "h" || key == "grow_h";
		auto fail = [&](const ScriptError &e) { return Some(e); };
		if (key == "parent" || key == "name")
			continue; // traités par l'appelant
		if (key == "w" || key == "h") {
			auto d = ParseDimension(v, fn);
			if (d.IsError())
				return fail(d.Error());
			if (key == "w")
				b.W(d.Value());
			else
				b.H(d.Value());
		} else if (key == "grow") {
			const float weight = v.IsNumber() ? v.AsFloat() : 1.f;
			if (v.IsTruthy())
				b.GrowW(weight);
		} else if (key == "grow_h") {
			if (v.IsTruthy())
				b.GrowH(v.IsNumber() ? v.AsFloat() : 1.f);
		} else if (key == "pad") {
			if (v.IsNumber())
				b.Pad(v.AsFloat());
			else if (v.IsList() && v.AsList() && v.AsList()->Size() == 2)
				b.Pad(math::Sides{v.AsList()->At(0).Unwrap().AsFloat(), v.AsList()->At(1).Unwrap().AsFloat()});
		} else if (key == "gap") {
			b.Gap(v.AsFloat());
		} else if (key == "bg" || key == "color" || key == "border") {
			auto c = ParseColor(v, fn);
			if (c.IsError())
				return fail(c.Error());
			if (key == "bg")
				b.Bg(c.Value());
			else if (key == "color")
				b.TextColor(c.Value());
			else
				b.BorderColor(c.Value());
		} else if (key == "radius") {
			b.Radius(v.AsFloat());
		} else if (key == "font_size") {
			b.FontSize(v.AsFloat());
		} else if (key == "bold") {
			b.Bold(v.IsTruthy());
		} else if (key == "italic") {
			b.Italic(v.IsTruthy());
		} else if (key == "align" || key == "justify" || key == "text_align" || key == "anchor") {
			const String s = v.ToDisplayString();
			if (key == "anchor") {
				Option<ui::Anchor> anchor = ParseAnchor(s);
				if (anchor.IsNone())
					return Some(Fail(String::Format("`%s` : ancre `%s` inconnue", fn, s.CStr())));
				b.Absolute().Anchor(anchor.Unwrap());
			} else if (key == "align") {
				b.Align(s == "center" ? ui::CrossAlign::Center
						: s == "end"  ? ui::CrossAlign::End
						: s == "stretch" ? ui::CrossAlign::Stretch
										 : ui::CrossAlign::Start);
			} else if (key == "justify") {
				b.Justify(s == "center" ? ui::Justify::Center
						  : s == "end"	? ui::Justify::End
						  : s == "between" ? ui::Justify::SpaceBetween
										   : ui::Justify::Start);
			} else {
				b.TextAlign(s == "center" ? ui::TextAlign::Center
							: s == "right" ? ui::TextAlign::Right
										   : ui::TextAlign::Left);
			}
		} else if (key == "offset") {
			if (v.IsList() && v.AsList() && v.AsList()->Size() == 2)
				b.Offset(v.AsList()->At(0).Unwrap().AsFloat(), v.AsList()->At(1).Unwrap().AsFloat());
		} else if (key == "absolute") {
			if (v.IsTruthy())
				b.Absolute();
		} else if (key == "fixed") {
			if (v.IsTruthy())
				b.Fixed();
		} else if (key == "hidden") {
			if (v.IsTruthy())
				b.Hidden();
		} else if (key == "pointer_through") {
			if (v.IsTruthy())
				b.PointerThrough();
		} else {
			return Some(Fail(String::Format("`%s` : option `%s` inconnue", fn, key.CStr())));
		}
	}
	// Sans taille donnée, un widget prend celle de son CONTENU, recalculée à
	// chaque mise en page (un texte qui s'allonge, une colonne qui gagne un
	// enfant). Il faut le dire explicitement : un widget sans taille déclarée
	// garderait sinon la taille de sa première mise en page (cf.
	// LayoutSystem::Pass::SizeVals). Un widget ancré s'aligne aussi par sa
	// taille (le coin qui correspond à l'ancre).
	if (!hasW)
		b.WAuto();
	if (!hasH)
		b.HAuto();
	return NONE;
}

// ── Synchronisation état → composants ───────────────────────────────────────

void PushText(WidgetObject &w);

void PushValue(WidgetObject &w);

void PushVisible(WidgetObject &w);

void PushStyle(WidgetObject &w, const std::function<void(ui::UiStyle &)> &edit);

/// Appelle `fn(widget[, valeur])` — les erreurs, sans appelant pour les
/// recevoir, passent par Interpreter::ReportError.
void Invoke(Interpreter &vm, const std::weak_ptr<std::atomic<bool>> &alive, const std::shared_ptr<WidgetObject> &w,
				   const Value &fn, Option<Value> argument);

// ── Création ────────────────────────────────────────────────────────────────

/// Ce que chaque constructeur décrit : le widget, et comment la fabrique le
/// construit (absent sans écran).
struct Spec {
	const char *kind;
	std::function<ui::WidgetBuilder(ui::UiFactory &, const WidgetObject &)> make;
};

[[nodiscard]] Result<Value, ScriptError> Create(Interpreter &vm, const std::shared_ptr<UiState> &state,
													   const Spec &spec, std::shared_ptr<WidgetObject> w,
													   const MapObject *opts, const char *fn);

/// Dernier argument : la table d'options, si c'en est une.
[[nodiscard]] const MapObject *OptionsOf(Args &args, size_t fixedArgs);

void RemoveTree(WidgetObject &w);

void DefineWidget(TypeBuilder &builder);

} // namespace uilib

/**
 * Installe l'espace de noms `ui` : constructeurs de widgets rattachés à la
 * racine que donne `binding` (cf. en-tête). `binding` nul : interface sans
 * écran (objets seulement).
 */
void InstallUiLibrary(Interpreter &vm, std::shared_ptr<UiBinding> binding);

} // namespace data::script
