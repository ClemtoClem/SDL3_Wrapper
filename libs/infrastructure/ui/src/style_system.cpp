// StyleSystem — cascade des styles (cf. style.hpp pour l'ordre de priorité)
#include <ui/systems.hpp>

#include <algorithm>
#include <unordered_set>

namespace ui {

namespace {

/// Feuilles visibles depuis un widget, de la portée la plus large à la plus proche.
struct Scope {
	ecs::Entity owner;
	const StyleSheet *sheet;
	int depth; ///< -1 : module ; 0 : application ; 1.. : imports des widgets
	int sheetIndex;
};

struct Match {
	const StyleProps *props;
	int depth;
	uint32_t specificity;
	int sheetIndex;
	size_t order;
};

bool operator<(const Match &a, const Match &b) {
	if (a.depth != b.depth)
		return a.depth < b.depth;
	if (a.specificity != b.specificity)
		return a.specificity < b.specificity;
	if (a.sheetIndex != b.sheetIndex)
		return a.sheetIndex < b.sheetIndex;
	return a.order < b.order;
}

bool SameProps(const StyleProps &a, const StyleProps &b) {
#define UI_X(T, member, css, inherited, def)                                                                         \
	if (a.member.IsSome() != b.member.IsSome())                                                                      \
		return false;                                                                                                \
	if (a.member.IsSome() && !(a.member.ValueUnchecked() == b.member.ValueUnchecked()))                              \
		return false;
	UI_STYLE_PROPERTIES(UI_X)
#undef UI_X
	if (a.customs.size() != b.customs.size())
		return false;
	for (size_t i = 0; i < a.customs.size(); ++i)
		if (a.customs[i].name != b.customs[i].name || a.customs[i].value != b.customs[i].value)
			return false;
	return true;
}

/// État calculé à partir des briques (en plus des états d'interaction).
State DerivedState(const ecs::ArchetypeRegistry &world, ecs::Entity e, bool disabled) {
	State s = State::None;
	if (disabled)
		s = s | State::Disabled;
	if (auto c = world.GetComponent<Check>(e); c.IsSome() && c.Unwrap()->on)
		s = s | State::Checked;
	if (auto item = world.GetComponent<SelectItem>(e); item.IsSome()) {
		const ecs::Entity owner = AncestorWith<Selection>(world, e);
		if (owner.Valid())
			if (auto sel = world.GetComponent<Selection>(owner); sel.IsSome() && sel.Unwrap()->IsSelected(item.Unwrap()->index))
				s = s | State::Selected;
	}
	if (auto p = world.GetComponent<PopupState>(e); p.IsSome() && p.Unwrap()->open)
		s = s | State::Open;
	if (world.HasComponent<PopupTrigger>(e)) {
		for (ecs::Entity child : ChildList(world, e))
			if (auto p = world.GetComponent<PopupState>(child); p.IsSome() && p.Unwrap()->open)
				s = s | State::Open;
	}
	if (auto edit = world.GetComponent<EditOnDoubleClick>(e); edit.IsSome() && edit.Unwrap()->editing)
		s = s | State::Focus;
	return s;
}

class Resolver {
public:
	Resolver(ecs::ArchetypeRegistry &w, Context &c) : world(w), ctx(c) {}

	ecs::ArchetypeRegistry &world;
	Context &ctx;
	bool propagateAll = true;
	size_t resolved = 0;
	std::unordered_set<ecs::Entity> dirty;

	/// Feuilles des ancêtres de `e` (exclu), de la racine vers le parent.
	std::vector<Scope> ScopesAbove(ecs::Entity e) {
		std::vector<ecs::Entity> chain;
		for (Option<ecs::Entity> cur = ecs::ParentOf(world, e); cur.IsSome(); cur = ecs::ParentOf(world, cur.Unwrap()))
			chain.push_back(cur.Unwrap());
		std::reverse(chain.begin(), chain.end());
		std::vector<Scope> scopes = BaseScopes();
		for (ecs::Entity a : chain)
			AppendScopes(scopes, a);
		return scopes;
	}

	std::vector<Scope> BaseScopes() {
		std::vector<Scope> scopes;
		int index = 0;
		for (const StyleSheetPtr &s : ctx.moduleSheets)
			scopes.push_back({ctx.root, s.get(), -1, index++});
		for (const StyleSheetPtr &s : ctx.sheets)
			scopes.push_back({ctx.root, s.get(), 0, index++});
		return scopes;
	}

	void AppendScopes(std::vector<Scope> &scopes, ecs::Entity e) {
		if (auto scope = world.GetComponent<StyleScope>(e); scope.IsSome()) {
			int depth = 1;
			for (const Scope &s : scopes)
				depth = std::max(depth, s.depth + 1);
			int index = int(scopes.size());
			for (const StyleSheetPtr &s : scope.Unwrap()->sheets)
				scopes.push_back({e, s.get(), depth, index++});
		}
	}

	void Resolve(ecs::Entity e, const StyleProps &inherited, float parentScale, float parentOpacity,
				 bool parentDisabled, std::vector<Scope> &scopes, bool force) {
		if (!world.IsAlive(e))
			return;
		const size_t scopeMark = scopes.size();
		AppendScopes(scopes, e);

		const bool disabled = parentDisabled || world.HasComponent<Disabled>(e);
		// :disabled ne vise que le widget désactivé lui-même (son opacité se
		// compose déjà sur ses descendants).
		State state = DerivedState(world, e, world.HasComponent<Disabled>(e));
		if (auto ws = world.GetComponent<WidgetState>(e); ws.IsSome())
			state = state | ws.Unwrap()->flags;

		// ── Règles qui s'appliquent ─────────────────────────────────────────
		std::vector<Match> shared, privates, module;
		for (const Scope &scope : scopes)
			for (const StyleRule &rule : scope.sheet->Rules())
				for (const Selector &sel : rule.selectors) {
					if (!MatchSubject(e, sel, scope.owner, state))
						continue;
					const Match m{&rule.props, scope.depth, sel.specificity, scope.sheetIndex, rule.order};
					if (scope.depth < 0)
						module.push_back(m);
					else if (sel.IsPrivate())
						privates.push_back(m);
					else
						shared.push_back(m);
					break; // une règle compte une fois, même si plusieurs sélecteurs correspondent
				}
		std::sort(module.begin(), module.end());
		std::sort(shared.begin(), shared.end());
		std::sort(privates.begin(), privates.end());

		const InlineStyle *inl = nullptr;
		Option<RefMut<InlineStyle>> inlineRef = world.GetComponent<InlineStyle>(e);
		if (inlineRef.IsSome())
			inl = inlineRef.Unwrap().Get();

		// ── Propriétés personnalisées (var) ─────────────────────────────────
		std::vector<CustomProperty> sharedVars = inherited.customs;
		auto addVars = [](std::vector<CustomProperty> &vars, const StyleProps &p) {
			for (const CustomProperty &c : p.customs) {
				bool found = false;
				for (CustomProperty &v : vars)
					if (v.name == c.name) {
						v.value = c.value;
						found = true;
					}
				if (!found)
					vars.push_back(c);
			}
		};
		for (const Match &m : module)
			addVars(sharedVars, *m.props);
		for (const Match &m : shared)
			addVars(sharedVars, *m.props);
		if (inl) {
			addVars(sharedVars, inl->shared);
			for (const StateStyle &st : inl->states)
				if (st.shared && HasState(state, st.mask))
					addVars(sharedVars, st.props);
		}
		std::vector<CustomProperty> ownVars = sharedVars;
		for (const Match &m : privates)
			addVars(ownVars, *m.props);
		if (inl) {
			addVars(ownVars, inl->own);
			for (const StateStyle &st : inl->states)
				if (!st.shared && HasState(state, st.mask))
					addVars(ownVars, st.props);
		}

		// ── Couches ─────────────────────────────────────────────────────────
		auto merge = [](StyleProps &into, const StyleProps &layer, const std::vector<CustomProperty> &vars) {
			if (layer.deferred.empty()) {
				into.Merge(layer);
				return;
			}
			StyleProps resolvedLayer = layer;
			resolvedLayer.deferred.clear();
			for (const DeferredDeclaration &d : layer.deferred)
				(void)SetDeclaration(resolvedLayer, d.property.View(), SubstituteVars(d.value.View(), vars).View());
			into.Merge(resolvedLayer);
		};
		StyleProps S; // partagé (héritable)
		for (const Match &m : module)
			merge(S, *m.props, sharedVars);
		for (const Match &m : shared)
			merge(S, *m.props, sharedVars);
		if (inl) {
			merge(S, inl->shared, sharedVars);
			for (const StateStyle &st : inl->states)
				if (st.shared && HasState(state, st.mask))
					merge(S, st.props, sharedVars);
		}
		S.deferred.clear();
		StyleProps O = S; // tout ce qui s'applique au widget
		for (const Match &m : privates)
			merge(O, *m.props, ownVars);
		if (inl) {
			merge(O, inl->own, ownVars);
			for (const StateStyle &st : inl->states)
				if (!st.shared && HasState(state, st.mask))
					merge(O, st.props, ownVars);
		}
		O.deferred.clear();

		// ── Valeurs calculées ───────────────────────────────────────────────
		ComputedStyle cs = ComputedStyle::Initial();
		cs.Apply(inherited);
		cs.Apply(O);
		cs.scale = parentScale * (cs.zoom > 0.f ? cs.zoom : 1.f);
		cs.fontSize *= cs.scale;
		cs.effectiveOpacity = parentOpacity * math::Clamp(cs.opacity, 0.f, 1.f);

		StyleProps childInherited = inherited;
		childInherited.MergeInherited(S);
		childInherited.customs = sharedVars;

		bool sharedChanged = true;
		if (auto old = world.GetComponent<SharedStyle>(e); old.IsSome()) {
			sharedChanged = !SameProps(old.Unwrap()->props, childInherited);
			old.Unwrap()->props = childInherited;
		} else {
			world.AddComponent(e, SharedStyle{childInherited});
		}
		float oldScale = -1.f, oldOpacity = -1.f;
		if (auto old = world.GetComponent<ComputedStyle>(e); old.IsSome()) {
			oldScale = old.Unwrap()->scale;
			oldOpacity = old.Unwrap()->effectiveOpacity;
			*old.Unwrap() = cs;
		} else {
			world.AddComponent(e, cs);
		}
		++resolved;

		const bool recurse = force || propagateAll || sharedChanged || oldScale != cs.scale ||
							 oldOpacity != cs.effectiveOpacity;
		// Copie : un rappel de style ne modifie jamais l'arbre, mais la liste
		// d'enfants vit dans un composant que AddComponent peut déplacer.
		const std::vector<ecs::Entity> children = ChildList(world, e);
		for (ecs::Entity child : children) {
			if (recurse || dirty.count(child))
				Resolve(child, childInherited, cs.scale, cs.effectiveOpacity, disabled, scopes, force);
			else
				ResolveDirtyBelow(child);
		}
		scopes.resize(scopeMark);
	}

	/// Sous-arbre inchangé : seuls ses widgets marqués sont restylés, à partir
	/// des valeurs mémorisées de leur parent.
	void ResolveDirtyBelow(ecs::Entity e) {
		for (ecs::Entity child : std::vector<ecs::Entity>(ChildList(world, e))) {
			if (dirty.count(child))
				ResolveFromParent(child);
			else
				ResolveDirtyBelow(child);
		}
	}

	void ResolveFromParent(ecs::Entity e) {
		StyleProps inherited;
		float scale = 1.f, opacity = 1.f;
		bool disabled = false;
		if (auto parent = ecs::ParentOf(world, e); parent.IsSome()) {
			if (auto s = world.GetComponent<SharedStyle>(parent.Unwrap()); s.IsSome())
				inherited = s.Unwrap()->props;
			if (auto c = world.GetComponent<ComputedStyle>(parent.Unwrap()); c.IsSome()) {
				scale = c.Unwrap()->scale;
				opacity = c.Unwrap()->effectiveOpacity;
			}
			disabled = IsDisabledRecursive(world, parent.Unwrap());
		}
		std::vector<Scope> scopes = ScopesAbove(e);
		Resolve(e, inherited, scale, opacity, disabled, scopes, false);
	}

	bool MatchSubject(ecs::Entity e, const Selector &sel, ecs::Entity owner, State state) const {
		// Fast path : le sujet d'abord, avec l'état déjà calculé.
		const CompoundSelector &subject = sel.Subject();
		ElementData data = DataOf(e, owner);
		data.state = state;
		if (!MatchCompound(subject, data))
			return false;
		if (sel.parts.size() == 1)
			return true;
		return MatchesSelector(world, e, sel, owner);
	}

	ElementData DataOf(ecs::Entity e, ecs::Entity owner) const {
		ElementData d;
		if (auto el = world.GetComponent<Element>(e); el.IsSome()) {
			d.type = el.Unwrap()->type.View();
			d.id = el.Unwrap()->id.View();
			d.classes = &el.Unwrap()->classes;
		}
		d.isRoot = e == ctx.root;
		d.isScopeOwner = e == owner;
		if (auto parent = ecs::ParentOf(world, e); parent.IsSome()) {
			const std::vector<ecs::Entity> &siblings = ChildList(world, parent.Unwrap());
			d.siblingCount = int(siblings.size());
			for (size_t i = 0; i < siblings.size(); ++i)
				if (siblings[i] == e)
					d.index = int(i);
		}
		return d;
	}
};

/// Vrai si une feuille contient des sélecteurs dont un ANCÊTRE dépend d'un
/// état ou de la structure : changer cet état peut alors modifier des
/// descendants dont les valeurs partagées n'ont pas bougé.
bool HasContextualSelectors(const Context &ctx) {
	auto check = [](const StyleSheetPtr &sheet) {
		for (const StyleRule &r : sheet->Rules())
			for (const Selector &s : r.selectors)
				if (s.parts.size() > 1)
					return true;
		return false;
	};
	for (const StyleSheetPtr &s : ctx.moduleSheets)
		if (check(s))
			return true;
	for (const StyleSheetPtr &s : ctx.sheets)
		if (check(s))
			return true;
	return false;
}

} // namespace

bool MatchesSelector(const ecs::ArchetypeRegistry &world, ecs::Entity e, const Selector &selector, ecs::Entity scopeOwner) {
	const Context *ctx = ContextOf(world);
	const ecs::Entity root = ctx ? ctx->root : ecs::Entity{};
	auto dataOf = [&](ecs::Entity x) {
		ElementData d;
		if (auto el = world.GetComponent<Element>(x); el.IsSome()) {
			d.type = el.Unwrap()->type.View();
			d.id = el.Unwrap()->id.View();
			d.classes = &el.Unwrap()->classes;
		}
		if (auto st = world.GetComponent<WidgetState>(x); st.IsSome())
			d.state = st.Unwrap()->flags;
		d.state = d.state | DerivedState(world, x, world.HasComponent<Disabled>(x));
		d.isRoot = x == root;
		d.isScopeOwner = x == scopeOwner || (!scopeOwner.Valid() && x == root);
		if (auto parent = ecs::ParentOf(world, x); parent.IsSome()) {
			const std::vector<ecs::Entity> &siblings = ChildList(world, parent.Unwrap());
			d.siblingCount = int(siblings.size());
			for (size_t i = 0; i < siblings.size(); ++i)
				if (siblings[i] == x)
					d.index = int(i);
		}
		return d;
	};
	if (selector.parts.empty())
		return false;
	// Correspondance de droite à gauche, avec retour arrière sur les descendants.
	std::function<bool(int, ecs::Entity)> matchFrom = [&](int part, ecs::Entity x) -> bool {
		if (!MatchCompound(selector.parts[size_t(part)], dataOf(x)))
			return false;
		if (part == 0)
			return true;
		const Combinator comb = selector.combinators[size_t(part - 1)];
		Option<ecs::Entity> up = ecs::ParentOf(world, x);
		if (comb == Combinator::Child)
			return up.IsSome() && matchFrom(part - 1, up.Unwrap());
		for (; up.IsSome(); up = ecs::ParentOf(world, up.Unwrap()))
			if (matchFrom(part - 1, up.Unwrap()))
				return true;
		return false;
	};
	return matchFrom(int(selector.parts.size()) - 1, e);
}

void StyleSystem::Run(ecs::ArchetypeRegistry &world, Context &ctx) {
	if (!ctx.styleDirtyAll && ctx.styleDirty.empty())
		return;
	Resolver r(world, ctx);
	r.propagateAll = HasContextualSelectors(ctx);
	if (ctx.styleDirtyAll) {
		ctx.styleDirtyAll = false;
		ctx.styleDirty.clear();
		std::vector<Scope> scopes = r.BaseScopes();
		r.Resolve(ctx.root, StyleProps{}, 1.f, 1.f, false, scopes, true);
		ctx.MarkLayoutDirty();
		return;
	}
	std::vector<ecs::Entity> dirty;
	dirty.swap(ctx.styleDirty);
	for (ecs::Entity e : dirty)
		r.dirty.insert(e);
	// Racines des sous-arbres à restyler : on ignore un widget dont un
	// ancêtre est lui-même à restyler (il sera atteint par la descente).
	for (ecs::Entity e : dirty) {
		if (!world.IsAlive(e))
			continue;
		bool covered = false;
		for (Option<ecs::Entity> up = ecs::ParentOf(world, e); up.IsSome(); up = ecs::ParentOf(world, up.Unwrap()))
			if (r.dirty.count(up.Unwrap())) {
				covered = true;
				break;
			}
		if (covered)
			continue;
		r.ResolveFromParent(e);
	}
	ctx.MarkLayoutDirty();
}

} // namespace ui
