// Définitions de kit.hpp
#include "kit.hpp"

namespace game_editor {

namespace kit {

sdl3::FColor Rgb(int r, int g, int b, int a) noexcept {
	return sdl3::FColor{float(r) / 255.f, float(g) / 255.f, float(b) / 255.f, float(a) / 255.f};
}

sdl3::Color ToColor(const sdl3::FColor &c) noexcept {
	auto channel = [](float v) { return uint8_t(sdl3::Clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
	return sdl3::Color{channel(c.r), channel(c.g), channel(c.b), 255};
}

float Luminance(const sdl3::FColor &c) noexcept {
	return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

sdl3::FColor Mix(const sdl3::FColor &a, const sdl3::FColor &b, float t) noexcept {
	return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.f};
}

Palette PaletteOf(const ui::UiTheme &theme) {
	const sdl3::FColor black{0.f, 0.f, 0.f, 1.f};
	const sdl3::FColor panel{theme.panelBg.r, theme.panelBg.g, theme.panelBg.b, 1.f};
	Palette p;
	p.light = Luminance(panel) > 0.5f;
	// Sombre : le fond de fenêtre est un panneau assombri (Atelier : panneau
	// 40 → fond 22, bandeau 30, barre d'outils 34). Clair : écarts plus
	// faibles, un gris clair reste lisible derrière du texte foncé.
	p.base = Mix(panel, black, p.light ? 0.12f : 0.45f);
	p.header = Mix(panel, black, p.light ? 0.06f : 0.25f);
	p.toolbar = Mix(panel, black, p.light ? 0.03f : 0.15f);
	p.well = Mix(panel, black, p.light ? 0.18f : 0.65f);
	const sdl3::FColor selected = theme.listbox.bgChecked;
	p.selection = selected.a > 0.f ? sdl3::FColor{selected.r, selected.g, selected.b, 1.f} : Mix(panel, theme.accent, 0.5f);
	p.separator = Mix(panel, theme.text, 0.2f);
	p.ok = p.light ? Rgb(20, 130, 40) : Rgb(96, 200, 96);
	p.warning = p.light ? Rgb(176, 104, 0) : Rgb(236, 176, 72);
	p.error = p.light ? Rgb(200, 40, 30) : Rgb(236, 88, 72);
	return p;
}

sdl3::FColor Readable(const UiContext &ctx, const sdl3::FColor &color) {
	if (!PaletteOf(ctx).light)
		return color;
	sdl3::FColor dark = Mix(color, sdl3::FColor{0.f, 0.f, 0.f, 1.f}, 0.45f);
	dark.a = color.a;
	return dark;
}

NodeLook LookOf(const scene::Node &node) {
	const String &type = node.type;
	if (type == node_kind::FOLDER)
		return {ui::MaterialIcons::FOLDER, Rgb(176, 176, 180), "Dossier"};
	if (Is2DNode(node)) {
		const sdl3::FColor violet = Rgb(176, 140, 240);
		if (type == node_kind::CANVAS_LAYER)
			return {ui::MaterialIcons::LAYERS, violet, "Calque d'interface"};
		if (Camera2DDesc::Has(node))
			return {ui::MaterialIcons::VIDEOCAM, violet, "Caméra 2D"};
		if (CanvasItemDesc::Has(node)) {
			switch (CanvasItemDesc::Read(node).kind) {
				case CanvasItemKind::TEXT:
					return {ui::MaterialIcons::TITLE, violet, "Texte 2D"};
				case CanvasItemKind::SPRITE:
					return {ui::MaterialIcons::IMAGE, violet, "Image 2D"};
				case CanvasItemKind::CIRCLE:
					return {ui::MaterialIcons::CIRCLE, violet, "Forme 2D"};
				case CanvasItemKind::POLYGON:
					return {ui::MaterialIcons::CHANGE_HISTORY, violet, "Forme 2D"};
				case CanvasItemKind::LINE:
					return {ui::MaterialIcons::POLYLINE, violet, "Trait 2D"};
				default:
					return {ui::MaterialIcons::CROP_SQUARE, violet, "Forme 2D"};
			}
		}
		return {ui::MaterialIcons::CROP_FREE, violet, "Nœud 2D"};
	}
	if (type == node_kind::LIGHT || (!VisualDesc::Has(node) && LightDesc::Has(node)))
		return {ui::MaterialIcons::LIGHTBULB, Rgb(246, 196, 64), "Lumière"};
	if (type == node_kind::CAMERA || (!VisualDesc::Has(node) && CameraNodeDesc::Has(node)))
		return {ui::MaterialIcons::VIDEOCAM, Rgb(196, 204, 220), "Caméra"};
	if (type == node_kind::TRIGGER || TriggerDesc::Has(node))
		return {ui::MaterialIcons::FILTER_ALT, Rgb(232, 92, 64), "Déclencheur"};
	if (type == node_kind::SPAWN)
		return {ui::MaterialIcons::PLACE, Rgb(120, 206, 128), "Repère"};
	if (VisualDesc::Has(node)) {
		if (VisualDesc::Read(node).shape == ShapeKind::MODEL)
			return {ui::MaterialIcons::CATEGORY, Rgb(206, 168, 120), "Modèle"};
		return {ui::MaterialIcons::VIEW_IN_AR, Rgb(166, 186, 216), "Maillage"};
	}
	if (type == node_kind::BODY || PhysicsDesc::Read(node).body != BodyKind::NONE)
		return {ui::MaterialIcons::SPORTS_BASEBALL, Rgb(120, 200, 180), "Corps"};
	return {ui::MaterialIcons::ACCOUNT_TREE, Rgb(126, 172, 232), "Nœud"};
}

ScriptBadge BadgeOf(const UiContext &ctx, const ScriptOutline &outline) {
	const Palette palette = PaletteOf(ctx);
	ScriptBadge badge{ui::MaterialIcons::CHECK_CIRCLE, palette.ok, outline.Summary(), false};
	if (outline.error.IsSome()) {
		badge.icon = ui::MaterialIcons::ERROR;
		badge.color = palette.error;
		badge.broken = true;
	} else if (outline.role == ScriptRole::INVALID) {
		badge.icon = ui::MaterialIcons::WARNING;
		badge.color = palette.warning;
		badge.broken = true;
	} else if (outline.role == ScriptRole::MODULE || outline.role == ScriptRole::EMPTY) {
		badge.icon = ui::MaterialIcons::EXTENSION;
		badge.color = ctx.Theme().muted;
	}
	return badge;
}

namespace syntax {

// ── Scheme ───────────────────────────────────────────────────────────────────

Scheme Scheme::Dark() noexcept {
	return {{0.78f, 0.52f, 0.86f, 1.f}, {0.80f, 0.58f, 0.42f, 1.f}, {0.70f, 0.83f, 0.58f, 1.f},
			{0.46f, 0.54f, 0.46f, 1.f}, {0.86f, 0.84f, 0.56f, 1.f}, {0.34f, 0.76f, 0.76f, 1.f},
			{0.61f, 0.80f, 0.98f, 1.f}, {0.98f, 0.82f, 0.18f, 1.f}, {0.34f, 0.61f, 0.84f, 1.f},
			Rgb(240, 110, 100),		  Rgb(236, 176, 72),		   Rgb(120, 206, 128),
			Rgb(140, 142, 148), Rgb(96, 204, 150)};
}

Scheme Scheme::Light() noexcept {
	return {Rgb(160, 40, 170), Rgb(163, 21, 21),  Rgb(9, 134, 88),	 Rgb(0, 128, 0),	 Rgb(121, 94, 38),
			Rgb(38, 127, 153), Rgb(0, 81, 168),	  Rgb(175, 110, 0),	 Rgb(0, 0, 255),	 Rgb(200, 40, 30),
			Rgb(176, 104, 0),  Rgb(20, 130, 40),  Rgb(110, 112, 118), Rgb(30, 128, 88)};
}

Scheme & Colors() noexcept {
	static Scheme scheme = Scheme::Dark();
	return scheme;
}

bool IsIdentStart(char c) noexcept {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (unsigned char)c >= 0x80;
}

size_t StringEnd(const String &line, size_t i) noexcept {
	const char quote = line[i];
	size_t j = i + 1;
	while (j < line.size() && line[j] != quote) {
		if (line[j] == '\\' && j + 1 < line.size())
			++j;
		++j;
	}
	return j < line.size() ? j + 1 : line.size();
}

size_t NumberEnd(const String &line, size_t i) noexcept {
	size_t j = i;
	while (j < line.size() && (IsDigit(line[j]) || line[j] == '.' || line[j] == 'e' || line[j] == 'E' ||
							   ((line[j] == '-' || line[j] == '+') && j > i && (line[j - 1] == 'e' || line[j - 1] == 'E'))))
		++j;
	return j;
}

void HighlightSled(const String &line, std::vector<ui::UiTextSpan> &out) {
	static constexpr const char *KEYWORDS[] = {"let",   "var",      "const", "namespace", "fn",       "func",
											   "if",    "else",     "while", "do",        "for",      "in",       "return",
											   "break", "continue", "and",   "or",        "not",      "true",
											   "false", "nil",      "null",
											   // classes et interfaces
											   "class", "interface", "abstract", "extends", "implements", "override",
											   "static", "factory", "new", "this", "super", "is", "as",
											   // exécution asynchrone
											   "async", "await",
											   // énumérés, surcharge d'opérateurs
											   "enum", "operator",
											   // modules
											   "import"};
	size_t i = 0;
	while (i < line.size()) {
		const char c = line[i];
		if (c == '#' || (c == '/' && i + 1 < line.size() && line[i + 1] == '/')) {
			out.push_back({i, line.size(), Colors().comment});
			return;
		}
		if (c == '"' || c == '\'') {
			const size_t end = StringEnd(line, i);
			out.push_back({i, end, Colors().string});
			i = end;
			continue;
		}
		if (IsDigit(c)) {
			const size_t end = NumberEnd(line, i);
			out.push_back({i, end, Colors().number});
			i = end;
			continue;
		}
		if (IsIdentStart(c)) {
			size_t end = i;
			while (end < line.size() && IsIdentChar(line[end]))
				++end;
			const String word = line.Substr(i, end - i);
			bool keyword = false;
			for (const char *k : KEYWORDS)
				if (word == k)
					keyword = true;
			const bool engineBase = !keyword && engine_base::ShortName(word).IsSome();
			if (keyword)
				out.push_back({i, end, Colors().keyword});
			else if (engineBase)
				out.push_back({i, end, Colors().type});
			else if (end < line.size() && line[end] == '.')
				out.push_back({i, end, Colors().nameSpace});
			else if (end < line.size() && line[end] == '(')
				out.push_back({i, end, Colors().function});
			i = end;
			continue;
		}
		++i;
	}
}

void HighlightJson(const String &line, std::vector<ui::UiTextSpan> &out) {
	size_t i = 0;
	while (i < line.size()) {
		const char c = line[i];
		if (c == '"') {
			const size_t end = StringEnd(line, i);
			size_t after = end;
			while (after < line.size() && line[after] == ' ')
				++after;
			out.push_back({i, end, (after < line.size() && line[after] == ':') ? Colors().key : Colors().string});
			i = end;
			continue;
		}
		if (IsDigit(c) || (c == '-' && i + 1 < line.size() && IsDigit(line[i + 1]))) {
			const size_t end = NumberEnd(line, i + 1);
			out.push_back({i, end, Colors().number});
			i = end;
			continue;
		}
		if (c == '{' || c == '}' || c == '[' || c == ']') {
			out.push_back({i, i + 1, Colors().brace});
			++i;
			continue;
		}
		if (IsIdentStart(c)) {
			size_t end = i;
			while (end < line.size() && IsIdentChar(line[end]))
				++end;
			const String word = line.Substr(i, end - i);
			if (word == "true" || word == "false" || word == "null")
				out.push_back({i, end, Colors().literal});
			i = end;
			continue;
		}
		++i;
	}
}

void HighlightLog(const String &line, std::vector<ui::UiTextSpan> &out) {
	if (line.size() < 2 || line[0] != '[')
		return;
	const size_t close = line.Find("]");
	if (close == String::NPOS)
		return;
	const String level = line.Substr(1, close - 1).Trim();
	if (level == "err")
		out.push_back({0, line.size(), Colors().error});
	else if (level == "warn")
		out.push_back({0, close + 1, Colors().warning});
	else if (level == "ok")
		out.push_back({0, close + 1, Colors().ok});
	else if (level == "script")
		out.push_back({0, close + 1, Colors().nameSpace});
	else
		out.push_back({0, close + 1, Colors().dim});
}

ui::UiSyntaxHighlighter ForName(const String &name) {
	const String lower = name.ToLower();
	if (lower.EndsWith(".json") || lower.EndsWith(".gltf"))
		return HighlightJson;
	return HighlightSled;
}

} // namespace syntax

void ClearChildren(UiContext &ctx, ecs::Entity parent) {
	std::vector<ecs::Entity> children;
	if (auto list = ctx.registry.GetComponent<ui::UiChildren>(parent); list.IsSome())
		children = list.Unwrap()->list;
	for (ecs::Entity child : children)
		ui::DespawnTree(ctx.registry, child);
	if (auto list = ctx.registry.GetComponent<ui::UiChildren>(parent); list.IsSome())
		list.Unwrap()->list.clear();
	ctx.gui.Layout().MarkDirty();
}

void SetBackground(UiContext &ctx, ecs::Entity entity, sdl3::FColor color) {
	auto style = ctx.registry.GetOrAddComponent<ui::UiStyle>(entity);
	if (style.IsNone())
		return;
	style.Unwrap()->SetBg(color);
	if (!ctx.registry.HasComponent<ui::UiStyleDirty>(entity))
		ctx.registry.AddComponent(entity, ui::UiStyleDirty{});
}

void SetLabelText(UiContext &ctx, ecs::Entity entity, const String &text) {
	if (auto label = ctx.registry.GetComponent<ui::UiLabel>(entity); label.IsSome())
		label.Unwrap()->text = text;
}

void SetHidden(UiContext &ctx, ecs::Entity entity, bool hidden) {
	if (!entity.Valid())
		return;
	if (hidden && !ctx.registry.HasComponent<ui::UiHidden>(entity))
		ctx.registry.AddComponent(entity, ui::UiHidden{});
	else if (!hidden)
		ctx.registry.RemoveComponent<ui::UiHidden>(entity);
	ctx.gui.Layout().MarkDirty();
}

ecs::Entity Caption(UiContext &ctx, ecs::Entity parent, const String &text, float size) {
	ui::WidgetBuilder label = ctx.factory.Label(text);
	label.FontSize(size).TextColor(ctx.Theme().muted).WAuto().HAuto().TextEllipsis().Parent(parent);
	return label.Spawn();
}

ecs::Entity Spacer(UiContext &ctx, ecs::Entity parent) {
	ui::WidgetBuilder spacer = ctx.factory.Row();
	spacer.Pad(0.f);
	spacer.GrowW().H(ui::Dimension::Px(1.f)).Parent(parent);
	return spacer.Spawn();
}

ecs::Entity Glyph(UiContext &ctx, ecs::Entity parent, ui::MaterialIcons icon, sdl3::FColor color, float size) {
	ui::WidgetBuilder glyph = ctx.factory.Icon(icon, size);
	glyph.TextColor(Readable(ctx, color)).Size(size + 2.f, size + 2.f).PointerThrough().Parent(parent);
	return glyph.Spawn();
}

ecs::Entity IconButton(UiContext &ctx, ecs::Entity parent, ui::MaterialIcons icon, const String &tooltip,
		std::function<void()> action, float size, ecs::Entity *outIcon,
		sdl3::FColor tint) {
	ui::WidgetBuilder button = ctx.factory.Button(String());
	button.Size(size, size).Bg(sdl3::FColor{0.f, 0.f, 0.f, 0.f}).Radius(4.f).Tooltip(tooltip).Parent(parent);
	button.OnClick(std::move(action));
	ecs::Entity entity = button.Spawn();
	ui::WidgetBuilder glyph = ctx.factory.Icon(icon, size * 0.68f);
	glyph.Parent(entity).Anchor(ui::Anchor::Center).Justify(ui::Justify::Center).Absolute().PointerThrough();
	glyph.TextColor(tint.a > 0.f ? Readable(ctx, tint) : ctx.Theme().text);
	ecs::Entity glyphEntity = glyph.Spawn();
	if (outIcon)
		*outIcon = glyphEntity;
	return entity;
}

ecs::Entity SearchField(UiContext &ctx, ecs::Entity parent, const String &placeholder,
		std::function<void(const String &)> onChange, float width) {
	ui::WidgetBuilder row = ctx.factory.Row();
	row.Pad(0.f);
	row.Gap(4.f).Align(ui::CrossAlign::Center).HAuto().Parent(parent);
	if (width > 0.f)
		row.W(ui::Dimension::Px(width));
	else
		row.GrowW();
	ecs::Entity rowEntity = row.Spawn();
	(void)Glyph(ctx, rowEntity, ui::MaterialIcons::SEARCH, ctx.Theme().muted, 15.f);
	ui::WidgetBuilder input = ctx.factory.Input(placeholder);
	input.GrowW().H(ui::Dimension::Px(24.f)).FontSize(13.f).Parent(rowEntity);
	input.OnTextChange(std::move(onChange));
	return input.Spawn();
}

// ── PropertyRows ─────────────────────────────────────────────────────────────

ecs::Entity PropertyRows::Vector2(ecs::Entity page, const char *label, math::FVector2 value, float speed,
								  std::function<void(math::FVector2)> onChange, float minValue, float maxValue) {
	ecs::Entity row = Row(page, label);
	auto state = std::make_shared<math::FVector2>(value);
	static constexpr const char *AXES[2] = {"X", "Y"};
	const sdl3::FColor colors[2] = {AXIS_X, AXIS_Y};
	for (int axis = 0; axis < 2; ++axis) {
		ui::WidgetBuilder letter = m_ctx.factory.Label(String(AXES[axis]));
		letter.FontSize(12.f).Bold().TextColor(colors[axis]).WAuto().HAuto().Parent(row);
		letter.Spawn();
		ui::WidgetBuilder field = m_ctx.factory.DragValue(minValue, maxValue, axis == 0 ? value.x : value.y, 0.f, speed, 1);
		field.GrowW().H(ui::Dimension::Px(22.f)).FontSize(12.f).Parent(row);
		field.OnChange([state, axis, onChange](float v) {
			(axis == 0 ? state->x : state->y) = v;
			onChange(*state);
		});
		field.Spawn();
	}
	return row;
}

ecs::Entity PropertyRows::Vector3(ecs::Entity page, const char *label, math::FVector3 value, float speed,
		std::function<void(math::FVector3)> onChange, float minValue,
		float maxValue) {
	ecs::Entity row = Row(page, label);
	auto state = std::make_shared<math::FVector3>(value);
	const float components[3] = {value.x, value.y, value.z};
	static constexpr const char *AXES[3] = {"X", "Y", "Z"};
	const sdl3::FColor colors[3] = {AXIS_X, AXIS_Y, AXIS_Z};
	for (int axis = 0; axis < 3; ++axis) {
		ui::WidgetBuilder letter = m_ctx.factory.Label(String(AXES[axis]));
		letter.FontSize(12.f).Bold().TextColor(colors[axis]).WAuto().HAuto().Parent(row);
		letter.Spawn();
		ui::WidgetBuilder field = m_ctx.factory.DragValue(minValue, maxValue, components[axis], 0.f, speed, 2);
		field.GrowW().H(ui::Dimension::Px(22.f)).FontSize(12.f).Parent(row);
		field.OnChange([state, axis, onChange](float v) {
			(axis == 0 ? state->x : axis == 1 ? state->y : state->z) = v;
			onChange(*state);
		});
		field.Spawn();
	}
	return row;
}

ecs::Entity PropertyRows::Number(ecs::Entity page, const char *label, float value, float minValue, float maxValue, float speed,
		std::function<void(float)> onChange, int decimals) {
	ecs::Entity row = Row(page, label);
	ui::WidgetBuilder field = m_ctx.factory.DragValue(minValue, maxValue, value, 0.f, speed, decimals);
	field.GrowW().H(ui::Dimension::Px(22.f)).FontSize(12.f).Parent(row);
	field.OnChange(std::move(onChange));
	field.Spawn();
	return row;
}

ecs::Entity PropertyRows::Slider(ecs::Entity page, const char *label, float value, float minValue, float maxValue,
		std::function<void(float)> onChange) {
	ecs::Entity row = Row(page, label);
	ui::WidgetBuilder slider = m_ctx.factory.Slider(minValue, maxValue, value);
	slider.GrowW().H(ui::Dimension::Px(18.f)).Parent(row);
	slider.OnChange(std::move(onChange));
	slider.Spawn();
	return row;
}

ecs::Entity PropertyRows::Check(ecs::Entity page, const char *label, bool value, std::function<void(bool)> onToggle) {
	ecs::Entity row = Row(page, label);
	ui::WidgetBuilder box = m_ctx.factory.Checkbox(value);
	box.Size(18.f, 18.f).Parent(row);
	box.OnToggle(std::move(onToggle));
	box.Spawn();
	return row;
}

ecs::Entity PropertyRows::Choice(ecs::Entity page, const char *label, std::vector<String> items, int selected,
		std::function<void(int)> onChange) {
	ecs::Entity row = Row(page, label);
	ui::WidgetBuilder combo = m_ctx.factory.Combo(std::move(items), selected);
	combo.GrowW().H(ui::Dimension::Px(24.f)).FontSize(12.f).Parent(row);
	combo.OnChange([onChange](float index) { onChange(int(index)); });
	combo.Spawn();
	return row;
}

ecs::Entity PropertyRows::Text(ecs::Entity page, const char *label, const String &value, std::function<void(const String &)> onSubmit) {
	ecs::Entity row = Row(page, label);
	ui::WidgetBuilder input = m_ctx.factory.Input();
	input.GrowW().H(ui::Dimension::Px(22.f)).FontSize(12.f).Parent(row);
	input.OnSubmit(std::move(onSubmit));
	ecs::Entity entity = input.Spawn();
	if (auto field = m_ctx.registry.GetComponent<ui::UiInput>(entity); field.IsSome())
		field.Unwrap()->text = value;
	return row;
}

ecs::Entity PropertyRows::ReadOnly(ecs::Entity page, const char *label, const String &value) {
	ecs::Entity row = Row(page, label);
	ui::WidgetBuilder text = m_ctx.factory.Label(value);
	text.GrowW().HAuto().FontSize(12.f).TextEllipsis().Parent(row);
	text.Spawn();
	return row;
}

ecs::Entity PropertyRows::Row(ecs::Entity page, const char *label) {
	ui::WidgetBuilder row = m_ctx.factory.Row();
	row.Pad(math::Sides{2.f, 1.f});
	row.Gap(4.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(page);
	ecs::Entity rowEntity = row.Spawn();
	ui::WidgetBuilder name = m_ctx.factory.Label(String(label));
	name.Size(m_labelWidth, 0.f).HAuto().FontSize(12.f).TextColor(m_ctx.Theme().muted).TextEllipsis();
	name.Parent(rowEntity);
	name.Spawn();
	return rowEntity;
}

// ── DockPanel ────────────────────────────────────────────────────────────────

ecs::Entity DockPanel::Build(ecs::Entity parent, const std::function<void(ui::WidgetBuilder &)> &size) {
	// Reconstruction (changement de thème) : l'ancien arbre a été détruit
	// avec la racine de l'éditeur, ses pages avec lui — les garder ici
	// dupliquait les onglets et pointait vers des entités mortes.
	m_pages.clear();
	m_active = -1;
	ui::WidgetBuilder frame = m_ctx.factory.Column();
	frame.Gap(0.f).Pad(0.f).Bg(m_ctx.Theme().panelBg).Parent(parent);
	size(frame);
	m_frame = frame.Spawn();

	ui::WidgetBuilder header = m_ctx.factory.Row();
	header.Gap(0.f).Pad(math::Sides{0.f, 0.f, 4.f, 0.f}).GrowW().H(ui::Dimension::Px(HEADER_HEIGHT));
	header.Align(ui::CrossAlign::Center).Bg(PaletteOf(m_ctx).header).Parent(m_frame);
	m_header = header.Spawn();

	ui::WidgetBuilder tabs = m_ctx.factory.Row();
	tabs.Pad(0.f);
	tabs.Gap(1.f).WAuto().GrowH().Align(ui::CrossAlign::End).Parent(m_header);
	m_tabs = tabs.Spawn();
	(void)Spacer(m_ctx, m_header);

	ui::WidgetBuilder extra = m_ctx.factory.Row();
	extra.Pad(0.f);
	extra.Gap(2.f).WAuto().HAuto().Align(ui::CrossAlign::Center).Parent(m_header);
	m_extra = extra.Spawn();
	m_menuButton = IconButton(m_ctx, m_header, ui::MaterialIcons::MENU, String("Menu du panneau"), [this] {
		if (!onMenu)
			return;
		if (auto computed = m_ctx.registry.GetComponent<ui::UiComputed>(m_menuButton); computed.IsSome()) {
			const sdl3::FRect r = computed.Unwrap()->screen;
			onMenu(r.x, r.y + r.h);
		}
	}, 22.f);

	ui::WidgetBuilder body = m_ctx.factory.Column();
	body.Gap(0.f).Pad(0.f).GrowW().GrowH().Clip().Parent(m_frame);
	m_body = body.Spawn();
	return m_frame;
}

ecs::Entity DockPanel::AddPage(const String &title, bool scroll, bool closable, float pad) {
	ui::WidgetBuilder page = m_ctx.factory.Column();
	page.Gap(4.f).Pad(pad).GrowW().GrowH().Parent(m_body);
	if (scroll)
		page.Scrollable().Clip();
	ecs::Entity entity = page.Spawn();
	m_pages.push_back(PageInfo{title, entity, closable});
	SetActive(int(m_pages.size()) - 1);
	return entity;
}

void DockPanel::RemovePage(int index) {
	if (index < 0 || index >= int(m_pages.size()))
		return;
	ui::DespawnTree(m_ctx.registry, m_pages[size_t(index)].content);
	if (auto list = m_ctx.registry.GetComponent<ui::UiChildren>(m_body); list.IsSome())
		std::erase(list.Unwrap()->list, m_pages[size_t(index)].content);
	m_pages.erase(m_pages.begin() + index);
	SetActive(sdl3::Min(m_active, int(m_pages.size()) - 1));
}

void DockPanel::SetActive(int index) {
	if (m_pages.empty()) {
		m_active = -1;
		RebuildTabs();
		return;
	}
	m_active = sdl3::Clamp(index, 0, int(m_pages.size()) - 1);
	for (size_t i = 0; i < m_pages.size(); ++i)
		SetHidden(m_ctx, m_pages[i].content, int(i) != m_active);
	RebuildTabs();
	if (onActivate)
		onActivate(m_active);
}

void DockPanel::SetTitle(int index, const String &title) {
	if (index < 0 || index >= int(m_pages.size()) || m_pages[size_t(index)].title == title)
		return;
	m_pages[size_t(index)].title = title;
	RebuildTabs();
}

ecs::Entity DockPanel::Page(int index) const {
	return index >= 0 && index < int(m_pages.size()) ? m_pages[size_t(index)].content : ecs::Entity{};
}

void DockPanel::RebuildTabs() {
	if (!m_tabs.Valid())
		return;
	ClearChildren(m_ctx, m_tabs);
	const ui::UiTheme &theme = m_ctx.Theme();
	for (size_t i = 0; i < m_pages.size(); ++i) {
		const bool active = int(i) == m_active;
		ui::WidgetBuilder tab = m_ctx.factory.Row();
		tab.Gap(0.f).Pad(math::Sides{2.f, 0.f, m_pages[i].closable ? 0.f : 2.f, 0.f}).WAuto();
		tab.H(ui::Dimension::Px(HEADER_HEIGHT - 3.f)).Align(ui::CrossAlign::Center).Parent(m_tabs);
		tab.Bg(active ? theme.panelBg : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
		ecs::Entity tabEntity = tab.Spawn();

		if (active) {
			// Liseré d'accent en haut de l'onglet actif (Godot/Unity).
			ui::WidgetBuilder accent = m_ctx.factory.Row();
			accent.Pad(0.f);
			accent.Absolute().Anchor(ui::Anchor::TopLeft).W(ui::Dimension::Rpct(100.f)).H(ui::Dimension::Px(2.f));
			accent.Bg(theme.accent).PointerThrough().Parent(tabEntity);
			accent.Spawn();
		}

		// PAS de Pad() sur un bouton : il en ferait un conteneur, dimensionné
		// par ses enfants (aucun) au lieu de son texte.
		ui::WidgetBuilder title = m_ctx.factory.Button(m_pages[i].title);
		title.H(ui::Dimension::Px(HEADER_HEIGHT - 6.f)).WAuto().FontSize(13.f)
			.Bg(sdl3::FColor{0.f, 0.f, 0.f, 0.f})
			.TextColor(active ? theme.text : theme.muted)
			.Parent(tabEntity);
		const int index = int(i);
		title.OnClick([this, index] { SetActive(index); });
		title.Spawn();

		if (m_pages[i].closable)
			(void)IconButton(m_ctx, tabEntity, ui::MaterialIcons::CLOSE, String("Fermer"), [this, index] {
				if (onClose)
					onClose(index);
			}, 18.f, nullptr, theme.muted);
	}
}

} // namespace kit

} // namespace game_editor
