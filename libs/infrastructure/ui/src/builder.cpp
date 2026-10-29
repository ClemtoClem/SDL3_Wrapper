// Définitions de ui/builder.hpp et de ui::Style (style_setters.hpp)
#include <ui/builder.hpp>

#include <algorithm>

namespace ui {

Style::Style(StringView css) {
	// Bloc de déclarations « a: b; c: d » (sans sélecteur ni accolades).
	size_t start = 0;
	int paren = 0;
	for (size_t i = 0; i <= css.GetSize(); ++i) {
		if (i < css.GetSize()) {
			if (css[i] == '(')
				++paren;
			else if (css[i] == ')')
				--paren;
			if (css[i] != ';' || paren > 0)
				continue;
		}
		const StringView decl = css.Substr(start, i - start);
		const size_t colon = decl.Find(':');
		if (colon != StringView::NPOS)
			(void)SetDeclaration(m_props, decl.Substr(0, colon), decl.Substr(colon + 1));
		start = i + 1;
	}
}

Widget AnyWidget::Build(ecs::ArchetypeRegistry &world, ecs::Entity parent, Option<size_t> index) const {
	if (!m_builder)
		return Widget();
	return m_builder->Build(world, parent, index);
}

Widget BuilderBase::Build(ecs::ArchetypeRegistry &world, ecs::Entity parent, Option<size_t> index) const {
	WidgetSpec s = spec;
	Expand(s);
	if (s.element.type.IsEmpty())
		s.element.type = "box";

	Context *ctx = ContextOf(world);
	if (!parent.Valid() && ctx)
		parent = ctx->root;

	const ecs::Entity e = world.SpawnBundle(std::move(s.element), std::move(s.style), WidgetState{}, ComputedStyle{},
											SharedStyle{}, Rect{}, std::move(s.bindings));
	if (parent.Valid() && world.IsAlive(parent)) {
		(void)ecs::SetParent(world, e, parent);
		if (index.IsSome())
			if (auto children = world.GetComponent<ecs::Children>(parent); children.IsSome()) {
				std::vector<ecs::Entity> &list = children.Unwrap()->entities;
				std::erase(list, e);
				const size_t at = std::min(index.Unwrap(), list.size());
				list.insert(list.begin() + std::ptrdiff_t(at), e);
			}
	}
	if (s.hidden)
		world.AddComponent(e, Hidden{});
	if (s.disabled)
		world.AddComponent(e, Disabled{});
	if (!s.tooltip.IsEmpty())
		world.AddComponent(e, Tooltip{s.tooltip});
	for (auto &attach : s.components)
		attach(world, e);

	if (!s.imports.empty() || !s.css.empty()) {
		StyleScope scope;
		for (const String &path : s.imports) {
			if (ctx) {
				if (auto sheet = ctx->LoadSheet(path); sheet.IsOk())
					scope.sheets.push_back(sheet.Value());
			} else if (auto sheet = StyleSheet::Load(path); sheet.IsOk()) {
				scope.sheets.push_back(sheet.Value());
			}
		}
		for (const String &text : s.css) {
			StyleSheetPtr sheet = StyleSheet::Parse(text.View(), String(), String("<css>"));
			if (ctx)
				ctx->RegisterFonts(*sheet);
			scope.sheets.push_back(std::move(sheet));
		}
		world.AddComponent(e, std::move(scope));
	}

	for (const AnyWidget &child : s.children)
		(void)child.Build(world, e);

	Widget w(world, e);
	for (Widget *ref : s.refs)
		if (ref)
			*ref = w;
	for (auto &fn : s.built)
		fn(w);
	if (ctx) {
		ctx->MarkStyleDirty(e);
		ctx->MarkLayoutDirty();
	}
	return w;
}

} // namespace ui
