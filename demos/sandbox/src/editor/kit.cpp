// Définitions de kit.hpp
#include <sandbox/editor/kit.hpp>

namespace sandbox {

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

Palette PaletteOf(const ui::Palette &theme) {
	const sdl3::FColor black{0.f, 0.f, 0.f, 1.f};
	const sdl3::FColor panel{theme.panel.r, theme.panel.g, theme.panel.b, 1.f};
	Palette p;
	p.light = Luminance(panel) > 0.5f;
	// Sombre : le fond de fenêtre est un panneau assombri (Atelier : panneau
	// 40 → fond 22, bandeau 30, barre d'outils 34). Clair : écarts plus
	// faibles, un gris clair reste lisible derrière du texte foncé.
	p.base = Mix(panel, black, p.light ? 0.12f : 0.45f);
	p.header = Mix(panel, black, p.light ? 0.06f : 0.25f);
	p.toolbar = Mix(panel, black, p.light ? 0.03f : 0.15f);
	p.well = Mix(panel, black, p.light ? 0.18f : 0.65f);
	p.selection = Mix(panel, theme.accent, 0.5f);
	p.separator = Mix(panel, theme.text, 0.2f);
	p.ok = p.light ? Rgb(20, 130, 40) : Rgb(96, 200, 96);
	p.warning = p.light ? Rgb(176, 104, 0) : Rgb(236, 176, 72);
	p.error = p.light ? Rgb(200, 40, 30) : Rgb(236, 88, 72);
	return p;
}

String EditorCss(const ui::Palette &theme) {
	const Palette p = PaletteOf(theme);
	auto c = [](const sdl3::FColor &color) { return ui::CssColor(color); };
	String css = String::Format(
		":root { --ed-base: %s; --ed-header: %s; --ed-toolbar: %s; --ed-well: %s; --ed-selection: %s; "
		"--ed-separator: %s; --ed-ok: %s; --ed-warning: %s; --ed-error: %s; }\n",
		c(p.base).CStr(), c(p.header).CStr(), c(p.toolbar).CStr(), c(p.well).CStr(), c(p.selection).CStr(),
		c(p.separator).CStr(), c(p.ok).CStr(), c(p.warning).CStr(), c(p.error).CStr());
	css.Append(R"css(
/* Cadre de l'éditeur */
.editor-base { background-color: var(--ed-base); }
.editor-header { background-color: var(--ed-header); }
.editor-toolbar { background-color: var(--ed-toolbar); }
.editor-well { background-color: var(--ed-well); }
.editor-separator { background-color: var(--ed-separator); }
.selected { background-color: var(--ed-selection); }
.caption { font-size: 12px; color: var(--muted); text-overflow: ellipsis; overflow: hidden; }
/* Boutons d'icône plats */
.flat-icon { background-color: transparent; border-width: 0; border-radius: 4px; padding: 0;
             justify-content: center; align-items: center; }
.flat-icon:hover { background-color: var(--hover); }
.flat-icon:active { background-color: var(--pressed); }
/* Lignes de propriétés */
.prop-row { padding: 1px 2px; gap: 4px; align-items: center; }
.prop-label { font-size: 12px; color: var(--muted); text-overflow: ellipsis; overflow: hidden; }
.prop-field { height: 22px; font-size: 12px; }
.axis-letter { font-size: 12px; font-weight: bold; }
.color-bar { height: 20px; border-radius: 3px; border-width: 0; }
/* Panneaux à onglets */
.dock { gap: 0; padding: 0; background-color: var(--panel); }
.dock-header { gap: 0; padding: 0 4px 0 0; height: 30px; align-items: center; background-color: var(--ed-header); }
.dock-tabs { gap: 1px; padding: 0; align-items: flex-end; height: 100%; }
.dock-tab { gap: 0; padding: 0 2px; height: 27px; align-items: center; background-color: transparent; }
.dock-tab.closable { padding: 0 0 0 2px; }
.dock-tab.active { background-color: var(--panel); border-top: 2px solid var(--accent); }
.dock-tab-title { height: 24px; font-size: 13px; background-color: transparent; border-width: 0; color: var(--muted); }
.dock-tab.active .dock-tab-title { color: var(--text); }
.dock-extra { gap: 2px; padding: 0; align-items: center; }
.dock-body { gap: 0; padding: 0; overflow: hidden; }
.dock-page { gap: 4px; }
.search-field { gap: 4px; padding: 0; align-items: center; }
.search-field > input { height: 24px; font-size: 13px; }
)css");
	return css;
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
	if (type == node_kind::AUDIO_SOURCE || (!VisualDesc::Has(node) && AudioSourceDesc::Has(node)))
		return {ui::MaterialIcons::VOLUME_UP, Rgb(236, 142, 196), "Source audio"};
	if (VisualDesc::Has(node)) {
		if (VisualDesc::Read(node).shape == ShapeKind::MODEL)
			return {ui::MaterialIcons::CATEGORY, Rgb(206, 168, 120), "Modèle"};
		if (VisualDesc::Read(node).shape == ShapeKind::MESH)
			return {ui::MaterialIcons::ARCHITECTURE, Rgb(232, 196, 120), "Maillage modélisé"};
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

void HighlightSled(const String &line, std::vector<ui::TextSpan> &out) {
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

void HighlightJson(const String &line, std::vector<ui::TextSpan> &out) {
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

void HighlightLog(const String &line, std::vector<ui::TextSpan> &out) {
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

ui::SyntaxHighlighter ForName(const String &name) {
	const String lower = name.ToLower();
	if (lower.EndsWith(".json") || lower.EndsWith(".gltf"))
		return HighlightJson;
	return HighlightSled;
}

} // namespace syntax

void ClearChildren(UiContext &, ui::Widget parent) {
	parent.Clear();
	if (auto selection = parent.Get<ui::Selection>(); selection.IsSome())
		selection.Unwrap()->selected.clear();
}

void MarkSelected(ui::Widget list, int index) {
	if (!list.Valid())
		return;
	if (list.Get<ui::Selection>().IsNone())
		list.World()->AddComponent(list.Entity(), ui::Selection{});
	list.Get<ui::Selection>().Unwrap()->SelectOnly(index);
}

void SetBackground(UiContext &, ui::Widget widget, sdl3::FColor color) { widget.Background(color); }

void SetLabelText(UiContext &, ui::Widget widget, const String &text) {
	if (widget && widget.Text() != text)
		widget.SetText(text);
}

void SetHidden(UiContext &, ui::Widget widget, bool hidden) {
	if (widget && widget.IsVisible() == hidden)
		widget.SetVisible(!hidden);
}

ui::Widget Caption(UiContext &, ui::Widget parent, const String &text, float size) {
	return parent.Append(ui::Label(text).Class("caption").FontSize(size));
}

ui::Widget Spacer(UiContext &, ui::Widget parent) { return parent.Append(ui::Spacer()); }

ui::Widget Glyph(UiContext &ctx, ui::Widget parent, ui::MaterialIcons icon, sdl3::FColor color, float size) {
	return parent.Append(ui::Icon(icon, size).Color(Readable(ctx, color)).Size(size + 2.f, size + 2.f).PointerThrough());
}

ui::Widget IconButton(UiContext &ctx, ui::Widget parent, ui::MaterialIcons icon, const String &tooltip,
					  std::function<void()> action, float size, ui::Widget *outIcon, sdl3::FColor tint) {
	ui::WidgetBuilder glyph = ui::Icon(icon, size * 0.68f);
	glyph.Color(tint.a > 0.f ? Readable(ctx, tint) : ctx.Theme().text).PointerThrough();
	if (outIcon)
		glyph.Ref(*outIcon);
	ui::WidgetBuilder button = ui::IconButton(glyph, tooltip);
	button.Class("flat-icon").Size(size, size).OnClick(std::move(action));
	return parent.Append(button);
}

ui::Widget SearchField(UiContext &ctx, ui::Widget parent, const String &placeholder,
					   std::function<void(const String &)> onChange, float width) {
	ui::WidgetBuilder row = ui::Row();
	row.Class("search-field");
	if (width > 0.f)
		row.Width(width);
	else
		row.GrowWidth();
	ui::Widget input;
	row.Children({
		ui::Icon(ui::MaterialIcons::SEARCH, 15.f).Color(Readable(ctx, ctx.Theme().muted)).Size(17.f, 17.f).PointerThrough(),
		ui::Input(placeholder).Grow().Ref(input).OnTextChange(std::move(onChange)),
	});
	parent.Append(row);
	return input;
}

// ── PropertyRows ─────────────────────────────────────────────────────────────

namespace {
/// Champ numérique glissable d'une ligne de propriétés.
ui::WidgetBuilder Drag(float minValue, float maxValue, float value, float speed, int decimals,
					   std::function<void(float)> onChange) {
	ui::WidgetBuilder field = ui::DragValue(minValue, maxValue, value, 0.f, speed, decimals);
	field.Class("prop-field").Grow().OnChange(std::move(onChange));
	return field;
}
} // namespace

ui::Widget PropertyRows::Vector2(ui::Widget page, const char *label, math::FVector2 value, float speed,
								 std::function<void(math::FVector2)> onChange, float minValue, float maxValue) {
	ui::Widget row = Row(page, label);
	auto state = std::make_shared<math::FVector2>(value);
	static constexpr const char *AXES[2] = {"X", "Y"};
	const sdl3::FColor colors[2] = {AXIS_X, AXIS_Y};
	for (int axis = 0; axis < 2; ++axis) {
		row.Append(ui::Label(AXES[axis]).Class("axis-letter").Color(colors[axis]));
		row.Append(Drag(minValue, maxValue, axis == 0 ? value.x : value.y, speed, 1, [state, axis, onChange](float v) {
			(axis == 0 ? state->x : state->y) = v;
			onChange(*state);
		}));
	}
	return row;
}

ui::Widget PropertyRows::Vector3(ui::Widget page, const char *label, math::FVector3 value, float speed,
								 std::function<void(math::FVector3)> onChange, float minValue, float maxValue) {
	ui::Widget row = Row(page, label);
	auto state = std::make_shared<math::FVector3>(value);
	const float components[3] = {value.x, value.y, value.z};
	static constexpr const char *AXES[3] = {"X", "Y", "Z"};
	const sdl3::FColor colors[3] = {AXIS_X, AXIS_Y, AXIS_Z};
	for (int axis = 0; axis < 3; ++axis) {
		row.Append(ui::Label(AXES[axis]).Class("axis-letter").Color(colors[axis]));
		row.Append(Drag(minValue, maxValue, components[axis], speed, 2, [state, axis, onChange](float v) {
			(axis == 0 ? state->x : axis == 1 ? state->y : state->z) = v;
			onChange(*state);
		}));
	}
	return row;
}

ui::Widget PropertyRows::Number(ui::Widget page, const char *label, float value, float minValue, float maxValue,
								float speed, std::function<void(float)> onChange, int decimals) {
	ui::Widget row = Row(page, label);
	row.Append(Drag(minValue, maxValue, value, speed, decimals, std::move(onChange)));
	return row;
}

ui::Widget PropertyRows::Slider(ui::Widget page, const char *label, float value, float minValue, float maxValue,
								std::function<void(float)> onChange) {
	ui::Widget row = Row(page, label);
	row.Append(ui::Slider(minValue, maxValue, value).Grow().Height(18.f).OnChange(std::move(onChange)));
	return row;
}

ui::Widget PropertyRows::Check(ui::Widget page, const char *label, bool value, std::function<void(bool)> onToggle) {
	ui::Widget row = Row(page, label);
	row.Append(ui::Checkbox(String(), value).OnToggle(std::move(onToggle)));
	return row;
}

ui::Widget PropertyRows::Choice(ui::Widget page, const char *label, std::vector<String> items, int selected,
								std::function<void(int)> onChange) {
	ui::Widget row = Row(page, label);
	row.Append(ui::Combo(std::move(items), selected).Class("prop-field").Height(24.f).Grow().OnSelect(std::move(onChange)));
	return row;
}

ui::Widget PropertyRows::Text(ui::Widget page, const char *label, const String &value,
							  std::function<void(const String &)> onSubmit) {
	ui::Widget row = Row(page, label);
	row.Append(ui::Input(String(), value).Class("prop-field").Grow().OnSubmit(std::move(onSubmit)));
	return row;
}

ui::Widget PropertyRows::ReadOnly(ui::Widget page, const char *label, const String &value) {
	ui::Widget row = Row(page, label);
	row.Append(ui::Label(value).FontSize(12.f).Ellipsis().Grow());
	return row;
}

ui::Widget PropertyRows::Color(ui::Widget page, const char *label, sdl3::Color value,
							   std::function<void(sdl3::Color)> onChange) {
	ui::Widget row = Row(page, label);
	// Construits l'un après l'autre : chaque rappel capture une poignée déjà valide.
	ui::Widget bar, popup;
	row.Append(ui::Button(String()).Class("color-bar").Grow().Background(FromColor(value)).Tooltip("Choisir la couleur").Ref(bar));
	row.Append(ui::Popup().Ref(popup).Children({
		ui::ColorPicker(FromColor(value), [bar, onChange](sdl3::FColor c) mutable {
			c.a = 1.f;
			bar.Background(c);
			onChange(ToColor(c));
		}),
	}));
	auto show = [bar, popup]() mutable { popup.OpenBelow(bar); };
	bar.OnClick(show);
	(void)IconButton(m_ctx, row, ui::MaterialIcons::COLORIZE, String("Sélecteur de couleur"), show, 22.f);
	return row;
}

ui::Widget PropertyRows::Row(ui::Widget page, const char *label) {
	ui::WidgetBuilder row = ui::Row();
	row.Class("prop-row").GrowWidth();
	row.Child(ui::Label(label).Class("prop-label").Width(m_labelWidth));
	return page.Append(row);
}

// ── DockPanel ────────────────────────────────────────────────────────────────

ui::Widget DockPanel::Build(ui::Widget parent, const std::function<void(ui::WidgetBuilder &)> &size) {
	// Reconstruction (changement de thème) : l'ancien arbre a été détruit
	// avec la racine de l'éditeur, ses pages avec lui.
	m_pages.clear();
	m_active = -1;
	ui::WidgetBuilder frame = ui::Column();
	frame.Class("dock");
	size(frame);
	frame.Children({
		ui::Row().Class("dock-header").GrowWidth().Ref(m_header).Children({
			ui::Row().Class("dock-tabs").Ref(m_tabs),
			ui::Spacer(),
			ui::Row().Class("dock-extra").Ref(m_extra),
		}),
		ui::Column().Class("dock-body").GrowWidth().GrowHeight().Ref(m_body),
	});
	m_frame = parent.Append(frame);
	m_menuButton = IconButton(m_ctx, m_header, ui::MaterialIcons::MENU, String("Menu du panneau"), [this] {
		if (!onMenu)
			return;
		const sdl3::FRect r = m_menuButton.Bounds();
		onMenu(r.x, r.y + r.h);
	}, 22.f);
	return m_frame;
}

ui::Widget DockPanel::AddPage(const String &title, bool scroll, bool closable, float pad) {
	ui::WidgetBuilder page = ui::Column();
	page.Class("dock-page").Padding(pad).GrowWidth().GrowHeight();
	if (scroll)
		page.Overflow(ui::Overflow::Auto);
	ui::Widget widget = m_body.Append(page);
	m_pages.push_back(PageInfo{title, widget, closable});
	SetActive(int(m_pages.size()) - 1);
	return widget;
}

void DockPanel::RemovePage(int index) {
	if (index < 0 || index >= int(m_pages.size()))
		return;
	m_pages[size_t(index)].content.Destroy();
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

ui::Widget DockPanel::Page(int index) const {
	return index >= 0 && index < int(m_pages.size()) ? m_pages[size_t(index)].content : ui::Widget();
}

void DockPanel::RebuildTabs() {
	if (!m_tabs.Valid())
		return;
	m_tabs.Clear();
	for (size_t i = 0; i < m_pages.size(); ++i) {
		const int index = int(i);
		ui::WidgetBuilder tab = ui::Row();
		tab.Class("dock-tab");
		if (int(i) == m_active)
			tab.Class("active");
		if (m_pages[i].closable)
			tab.Class("closable");
		tab.Child(ui::Button(m_pages[i].title).Class("dock-tab-title").OnClick([this, index] { SetActive(index); }));
		ui::Widget tabWidget = m_tabs.Append(tab);
		if (m_pages[i].closable)
			(void)IconButton(m_ctx, tabWidget, ui::MaterialIcons::CLOSE, String("Fermer"), [this, index] {
				if (onClose)
					onClose(index);
			}, 18.f, nullptr, m_ctx.Theme().muted);
	}
}

} // namespace kit

} // namespace sandbox
