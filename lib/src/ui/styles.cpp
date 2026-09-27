// Définitions de ui/styles.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "ui/styles.hpp"

namespace ui {

// ── UiStyle ──────────────────────────────────────────────────────────────────

void UiStyle::Merge(const UiStyle &other) {
	for (const auto &[k, v] : other.entries)
		entries[k] = v;
}

UiStyle UiStyle::Glass(sdl3::FColor base, float gloss, float glow, float radius) {
	auto lighten = [](float v) { return sdl3::Clamp(v + 45.f / 255.f, 0.f, 1.f); };
	sdl3::FColor top(lighten(base.r), lighten(base.g), lighten(base.b), base.a);
	UiStyle s;
	s.SetBg(top)
		.SetBgGradient(base)
		.SetBorderColor(sdl3::FColor::UI_WHITE_SOFT())
		.SetBordersWidth(1.f)
		.SetBordersRadius(radius)
		.SetGloss(gloss)
		.SetGlow(glow)
		.SetGlowColor(sdl3::FColor::UI_WHITE_STRONG());
	return s;
}

UiStyle UiStyle::GlassPanel(sdl3::FColor base) {
	return Glass(base, 0.28f, 0.35f, 10.f);
}

UiStyle UiStyle::GlassButton(sdl3::FColor base) {
	return Glass(base, 0.45f, 0.5f, 6.f);
}

// ── UiStyleSheet ─────────────────────────────────────────────────────────────

UiStyleSheet & UiStyleSheet::Define(String name, UiStyle style) {
	classes[String(name.c_str())] = std::move(style);
	return *this;
}

Option<const UiStyle *> UiStyleSheet::Lookup(const String &name) const {
	auto it = classes.find(String(name.c_str()));
	if (it != classes.end())
		return Some(&it->second);
	return NONE;
}

void UiStyleSheet::DirtyClassUsers(ecs::ArchetypeRegistry &world, const String &name) {
	world.Query<UiClassList>([&](ecs::Entity e, UiClassList &cls) {
		for (const auto &n : cls.names) {
			if (n == name) {
				if (!world.HasComponent<UiStyleDirty>(e))
					world.AddComponent(e, UiStyleDirty{});
				MarkSubtreeDirty(world, e);
				break;
			}
		}
	});
}

void UiStyleSheet::DirtyAllUsers(ecs::ArchetypeRegistry &world) {
	std::vector<ecs::Entity> users;
	// Collecte d'abord, mutation ensuite : AddComponent pendant un Query
	// invalide l'itération (cf. l'avertissement en tête de ecs.hpp).
	world.Query<UiClassList>([&](ecs::Entity e, UiClassList &) { users.push_back(e); });
	for (ecs::Entity e : users) {
		if (!world.HasComponent<UiStyleDirty>(e))
			world.AddComponent(e, UiStyleDirty{});
		MarkSubtreeDirty(world, e);
	}
}

// ── StyleSystem ──────────────────────────────────────────────────────────────

void StyleSystem::Resolve(ecs::ArchetypeRegistry &world) {
	// Collecte d'abord (retirer UiStyleDirty migre l'entité vers un
	// autre archétype ; le faire PENDANT un Query<UiStyleDirty> invalide
	// l'itération en cours — d'où la collecte préalable dans un vector).
	std::vector<ecs::Entity> dirtyEntities;
	world.Query<UiStyleDirty>([&](ecs::Entity e, UiStyleDirty &) { dirtyEntities.push_back(e); });
	if (dirtyEntities.empty())
		return;

	// Résout depuis chaque racine (entité sans UiParent). Collecte
	// D'ABORD (même raison que dirtyEntities ci-dessus) : resolveEntity()
	// appelle get_or_add_component<UiComputedStyle> pour CHAQUE entité de
	// tout le sous-arbre — une migration d'archétype par entité pas
	// encore résolue une fois, qui peut réallouer le vecteur
	// d'archétypes. L'appeler PENDANT ce Query<UiComputed> corromprait
	// son itération exactement comme pour UiStyleDirty plus haut ; ça
	// n'avait jamais crashé jusqu'ici par pure chance (pas assez
	// d'archétypes DISTINCTS dans les scènes de test précédentes pour
	// déclencher une réallocation au bon moment) — confirmé crash réel
	// sur ui_aero_basics.cpp (Phase 10), dont la variété de widgets crée
	// nettement plus d'archétypes distincts que les tests antérieurs.
	std::vector<ecs::Entity> roots;
	world.Query<UiComputed>([&](ecs::Entity e, UiComputed &) {
		if (!world.HasComponent<UiParent>(e))
			roots.push_back(e);
	});
	for (ecs::Entity e : roots)
		ResolveEntity(world, e, nullptr);

	// Nettoie tous les marqueurs dirty collectés plus haut.
	for (ecs::Entity e : dirtyEntities)
		world.RemoveComponent<UiStyleDirty>(e);
}

void StyleSystem::ResolveEntity(ecs::ArchetypeRegistry &world, ecs::Entity e, const UiStyle *parentResolved) {
	UiStyle result;

	// 1) Héritage depuis le parent (seules les propriétés héritables :
	// text-color, font-size, text-align, opacity, inner-zoom — comme en
	// CSS où background/border/padding n'héritent jamais par défaut).
	if (parentResolved) {
		if (auto *p = parentResolved->Get<prop::TextColor>())
			result.Set(*p);
		if (auto *p = parentResolved->Get<prop::FontSize>())
			result.Set(*p);
		if (auto *p = parentResolved->Get<prop::TextAlignProp>())
			result.Set(*p);
		if (auto *p = parentResolved->Get<prop::Opacity>())
			result.Set(*p);
		if (auto *p = parentResolved->Get<prop::InnerZoom>())
			result.Set(*p);
	}

	// 2) Classes (dans l'ordre de la liste ; la dernière gagne). La liste
	// commence toujours par "root"/"root-<type>" — cf. UiFactory::Spawn().
	if (auto classes = world.GetComponent<UiClassList>(e); classes.IsSome()) {
		for (const auto &name : classes.Unwrap()->names) {
			if (sheet) {
				if (auto s = sheet->Lookup(name); s.IsSome())
					result.Merge(*s.Unwrap());
			}
		}
	}

	// 3) Style inline (priorité maximale)
	if (auto inlineStyle = world.GetComponent<UiStyle>(e); inlineStyle.IsSome())
		result.Merge(*inlineStyle.Unwrap());

	// 4) Stocker le résultat
	auto stored = world.GetOrAddComponent<UiComputedStyle>(e);
	if (stored.IsSome())
		stored.Unwrap()->style = result;

	// 5) Propager le résultat résolu aux enfants pour l'héritage.
	//
	// Deux pièges d'invalidation ici, tous deux DÉJÀ RENCONTRÉS en vrai
	// (plantage use-after-free reproductible dès qu'une interface a assez
	// d'archétypes distincts — cf. examples/game_editor/, dont
	// l'inspecteur/outliner en crée beaucoup) :
	//
	//  a) on passait aux enfants un pointeur vers le `UiComputedStyle`
	//     STOCKÉ de cette entité. Or chaque appel récursif fait un
	//     `GetOrAddComponent<UiComputedStyle>` sur un enfant, ce qui peut
	//     faire migrer cet enfant d'archétype et RÉALLOUER le vecteur de
	//     composants où vit le nôtre : le pointeur du parent pendouille
	//     dès le premier enfant qui n'avait pas encore de style calculé.
	//     `result` est une copie locale, sur la pile de CET appel, avec
	//     exactement la même valeur — elle survit à toute la récursion.
	//
	//  b) la liste d'enfants était parcourue DIRECTEMENT dans le
	//     composant `UiChildren`, lui aussi susceptible d'être déplacé
	//     par ces mêmes migrations. On la copie donc avant d'itérer.
	std::vector<ecs::Entity> children;
	if (auto list = world.GetComponent<UiChildren>(e); list.IsSome())
		children = list.Unwrap()->list;
	for (ecs::Entity child : children)
		ResolveEntity(world, child, &result);
}

// ── ResolvedStyle ────────────────────────────────────────────────────────────

bool ResolvedStyle::Strikethrough(bool fallback) const noexcept {
	return Value<prop::Strikethrough>(fallback);
}

sdl3::FColor ResolvedStyle::HighlightColor(sdl3::FColor fallback) const noexcept {
	return Value<prop::HighlightTextColor>(fallback);
}

sdl3::FColor ResolvedStyle::BgHovered(sdl3::FColor fallback) const noexcept {
	return Value<prop::HoveredBackgroundColor>(fallback);
}

sdl3::FColor ResolvedStyle::BgPressed(sdl3::FColor fallback) const noexcept {
	return Value<prop::PressedBackgroundColor>(fallback);
}

sdl3::FColor ResolvedStyle::BgChecked(sdl3::FColor fallback) const noexcept {
	return Value<prop::CheckedBackgroundColor>(fallback);
}

Option<sdl3::FColor> ResolvedStyle::BgGradientOpt() const noexcept {
	if (style)
		if (const auto *p = style->Get<prop::BackgroundGradient>())
			return Some(p->value);
	return NONE;
}

ResolvedStyle GetResolved(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (auto c = world.GetComponent<UiComputedStyle>(e); c.IsSome())
		return ResolvedStyle(&c.Unwrap()->style);
	return ResolvedStyle{};
}

namespace detail {

const std::unordered_map<String, BoolSetter> & BoolPropRegistry() {
	static const std::unordered_map<String, BoolSetter> TABLE = {
		{prop::Enable::kName, [](UiStyle &s, bool v) { s.Set(prop::Enable{v}); }},
		{prop::Visible::kName, [](UiStyle &s, bool v) { s.Set(prop::Visible{v}); }},
		{prop::Italic::kName, [](UiStyle &s, bool v) { s.Set(prop::Italic{v}); }},
		{prop::Bold::kName, [](UiStyle &s, bool v) { s.Set(prop::Bold{v}); }},
		{prop::Underline::kName, [](UiStyle &s, bool v) { s.Set(prop::Underline{v}); }},
		{prop::Strikethrough::kName, [](UiStyle &s, bool v) { s.Set(prop::Strikethrough{v}); }},
		{prop::Highlight::kName, [](UiStyle &s, bool v) { s.Set(prop::Highlight{v}); }},
	};
	return TABLE;
}

const std::unordered_map<String, ColorSetter> & ColorPropRegistry() {
	static const std::unordered_map<String, ColorSetter> TABLE = {
		{prop::BackgroundColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::BackgroundColor{c}); }},
		{prop::BackgroundGradient::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::BackgroundGradient{c}); }},
		{prop::HoveredBackgroundColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::HoveredBackgroundColor{c}); }},
		{prop::PressedBackgroundColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::PressedBackgroundColor{c}); }},
		{prop::CheckedBackgroundColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::CheckedBackgroundColor{c}); }},
		{prop::FocusedBackgroundColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::FocusedBackgroundColor{c}); }},
		{prop::TextColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::TextColor{c}); }},
		{prop::HoveredTextColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::HoveredTextColor{c}); }},
		{prop::PressedTextColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::PressedTextColor{c}); }},
		{prop::CheckedTextColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::CheckedTextColor{c}); }},
		{prop::FocusedTextColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::FocusedTextColor{c}); }},
		{prop::BorderColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::BorderColor{c}); }},
		{prop::HoveredBorderColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::HoveredBorderColor{c}); }},
		{prop::PressedBorderColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::PressedBorderColor{c}); }},
		{prop::CheckedBorderColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::CheckedBorderColor{c}); }},
		{prop::FocusedBorderColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::FocusedBorderColor{c}); }},
		{prop::GlowColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::GlowColor{c}); }},
		{prop::HighlightTextColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::HighlightTextColor{c}); }},
	};
	return TABLE;
}

const std::unordered_map<String, FloatSetter> & FloatPropRegistry() {
	static const std::unordered_map<String, FloatSetter> TABLE = {
		{prop::FontSize::kName, [](UiStyle &s, float v) { s.Set(prop::FontSize{v}); }},
		{prop::InnerZoom::kName, [](UiStyle &s, float v) { s.Set(prop::InnerZoom{v}); }},
		{prop::Gap::kName, [](UiStyle &s, float v) { s.Set(prop::Gap{v}); }},
		{prop::Opacity::kName, [](UiStyle &s, float v) { s.Set(prop::Opacity{v}); }},
		{prop::Gloss::kName, [](UiStyle &s, float v) { s.Set(prop::Gloss{v}); }},
		{prop::Glow::kName, [](UiStyle &s, float v) { s.Set(prop::Glow{v}); }},
	};
	return TABLE;
}

const std::unordered_map<String, AlignSetter> & AlignPropRegistry() {
	static const std::unordered_map<String, AlignSetter> TABLE = {
		{prop::TextAlignProp::kName, [](UiStyle &s, TextAlign a) { s.Set(prop::TextAlignProp{a}); }},
	};
	return TABLE;
}

const std::unordered_map<String, SidesSetter> & SidesPropRegistry() {
	static const std::unordered_map<String, SidesSetter> TABLE = {
		{prop::Padding::kName, [](UiStyle &s, math::Sides v) { s.Set(prop::Padding{v}); }},
		{prop::Margin::kName, [](UiStyle &s, math::Sides v) { s.Set(prop::Margin{v}); }},
		{prop::BordersWidth::kName, [](UiStyle &s, math::Sides v) { s.Set(prop::BordersWidth{v}); }},
	};
	return TABLE;
}

const std::unordered_map<String, CornersSetter> & CornersPropRegistry() {
	static const std::unordered_map<String, CornersSetter> TABLE = {
		{prop::BordersRadius::kName, [](UiStyle &s, math::Corners v) { s.Set(prop::BordersRadius{v}); }},
	};
	return TABLE;
}

} // namespace detail

bool SetStyleProp(UiStyle &style, const String &name, bool value) {
	auto &table = detail::BoolPropRegistry();
	auto it = table.find(String(name.c_str()));
	if (it == table.end())
		return false;
	it->second(style, value);
	return true;
}

bool SetStyleProp(UiStyle &style, const String &name, sdl3::FColor value) {
	auto &table = detail::ColorPropRegistry();
	auto it = table.find(String(name.c_str()));
	if (it == table.end())
		return false;
	it->second(style, value);
	return true;
}

bool SetStyleProp(UiStyle &style, const String &name, float value) {
	auto &table = detail::FloatPropRegistry();
	auto it = table.find(String(name.c_str()));
	if (it == table.end())
		return false;
	it->second(style, value);
	return true;
}

bool SetStyleProp(UiStyle &style, const String &name, TextAlign value) {
	auto &table = detail::AlignPropRegistry();
	auto it = table.find(String(name.c_str()));
	if (it == table.end())
		return false;
	it->second(style, value);
	return true;
}

bool SetStyleProp(UiStyle &style, const String &name, math::Sides value) {
	auto &table = detail::SidesPropRegistry();
	auto it = table.find(String(name.c_str()));
	if (it == table.end())
		return false;
	it->second(style, value);
	return true;
}

bool SetStyleProp(UiStyle &style, const String &name, math::Corners value) {
	auto &table = detail::CornersPropRegistry();
	auto it = table.find(String(name.c_str()));
	if (it == table.end())
		return false;
	it->second(style, value);
	return true;
}

void SetInlineStyle(ecs::ArchetypeRegistry &world, ecs::Entity e, UiStyle style) {
	// Retirer l'ancien s'il existe pour éviter la duplication dans l'archétype.
	world.RemoveComponent<UiStyle>(e);
	world.AddComponent(e, std::move(style));
	if (!world.HasComponent<UiStyleDirty>(e))
		world.AddComponent(e, UiStyleDirty{});
	MarkSubtreeDirty(world, e);
}

void EditInlineStyle(ecs::ArchetypeRegistry &world, ecs::Entity e, std::function<void(UiStyle &)> fn) {
	auto existing = world.GetComponent<UiStyle>(e);
	UiStyle s;
	if (existing.IsSome())
		s = *existing.Unwrap();
	fn(s);
	SetInlineStyle(world, e, std::move(s));
}

void AddClass(ecs::ArchetypeRegistry &world, ecs::Entity e, String name) {
	auto cls = world.GetOrAddComponent<UiClassList>(e);
	if (cls.IsNone())
		return;
	auto &names = cls.Unwrap()->names;
	for (const auto &n : names)
		if (n == name)
			return; // déjà présente
	names.push_back(std::move(name));
	if (!world.HasComponent<UiStyleDirty>(e))
		world.AddComponent(e, UiStyleDirty{});
	MarkSubtreeDirty(world, e);
}

void RemoveClass(ecs::ArchetypeRegistry &world, ecs::Entity e, const String &name) {
	auto cls = world.GetComponent<UiClassList>(e);
	if (cls.IsNone())
		return;
	auto &names = cls.Unwrap()->names;
	auto it = std::find_if(names.begin(), names.end(), [&](const String &s) { return s == name; });
	if (it != names.end()) {
		names.erase(it);
		if (names.empty())
			world.RemoveComponent<UiClassList>(e);
		if (!world.HasComponent<UiStyleDirty>(e))
			world.AddComponent(e, UiStyleDirty{});
		MarkSubtreeDirty(world, e);
	}
}

bool ToggleClass(ecs::ArchetypeRegistry &world, ecs::Entity e, String name) {
	bool present = HasClass(world, e, name);
	if (present)
		RemoveClass(world, e, name);
	else
		AddClass(world, e, std::move(name));
	return !present;
}

bool HasClass(ecs::ArchetypeRegistry &world, ecs::Entity e, const String &name) {
	auto cls = world.GetComponent<UiClassList>(e);
	if (cls.IsNone())
		return false;
	for (const auto &n : cls.Unwrap()->names)
		if (n == name)
			return true;
	return false;
}

void SetClasses(ecs::ArchetypeRegistry &world, ecs::Entity e, std::vector<String> names) {
	if (names.empty()) {
		world.RemoveComponent<UiClassList>(e);
	} else {
		world.RemoveComponent<UiClassList>(e);
		world.AddComponent(e, UiClassList{std::move(names)});
	}
	if (!world.HasComponent<UiStyleDirty>(e))
		world.AddComponent(e, UiStyleDirty{});
	MarkSubtreeDirty(world, e);
}

void RemoveInlineStyle(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (world.HasComponent<UiStyle>(e)) {
		world.RemoveComponent<UiStyle>(e);
		if (!world.HasComponent<UiStyleDirty>(e))
			world.AddComponent(e, UiStyleDirty{});
		MarkSubtreeDirty(world, e);
	}
}

void MarkSubtreeDirty(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (auto children = world.GetComponent<UiChildren>(e); children.IsSome()) {
		for (ecs::Entity c : children.Unwrap()->list) {
			if (!world.HasComponent<UiStyleDirty>(c)) {
				world.AddComponent(c, UiStyleDirty{});
				MarkSubtreeDirty(world, c);
			}
		}
	}
}

void MarkStyleDirty(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (!world.HasComponent<UiStyleDirty>(e))
		world.AddComponent(e, UiStyleDirty{});
}

float GetEffectiveFontSize(ecs::ArchetypeRegistry &world, ecs::Entity e, float themeDefault) {
	return GetResolved(world, e).FontSize(themeDefault);
}

math::Sides GetEffectivePadding(ecs::ArchetypeRegistry &world, ecs::Entity e, math::Sides fallback) {
	math::Sides flowPad = fallback;
	if (auto f = world.GetComponent<UiFlow>(e); f.IsSome())
		flowPad = f.Unwrap()->padding;
	return GetResolved(world, e).Padding(flowPad);
}

float GetEffectiveGap(ecs::ArchetypeRegistry &world, ecs::Entity e, float fallback) {
	float flowGap = fallback;
	if (auto f = world.GetComponent<UiFlow>(e); f.IsSome())
		flowGap = f.Unwrap()->gap;
	return GetResolved(world, e).Gap(flowGap);
}

float GetEffectiveOpacity(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	return GetResolved(world, e).Opacity(1.f);
}

} // namespace ui
