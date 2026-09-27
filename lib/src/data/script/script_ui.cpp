// Définitions de data/script/script_ui.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "data/script.hpp"
#include "data/script/script_ui.hpp"

namespace data::script {

// ── UiBinding ────────────────────────────────────────────────────────────────

std::shared_ptr<UiBinding> UiBinding::FromUi(ui::Ui &gui, std::function<ecs::Entity()> rootProvider) {
	auto binding = std::make_shared<UiBinding>();
	binding->registry = &gui.World();
	binding->factory = &gui.Factory();
	binding->layout = &gui.Layout();
	binding->root = std::move(rootProvider);
	return binding;
}

ecs::Entity UiBinding::Root() const {
	if (!registry || !factory || !layout || !root)
		return ecs::Entity{};
	const ecs::Entity entity = root();
	return entity.Valid() && registry->IsAlive(entity) ? entity : ecs::Entity{};
}

bool UiBinding::Alive(ecs::Entity entity) const {
	return registry && entity.Valid() && registry->IsAlive(entity);
}

void UiBinding::Relayout() const {
	if (layout)
		layout->MarkDirty();
}

void UiBinding::Post(std::function<void()> callback) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_pending.push_back(std::move(callback));
}

size_t UiBinding::Flush() {
	std::vector<std::function<void()>> pending;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		pending.swap(m_pending);
	}
	for (auto &callback : pending)
		callback();
	return pending.size();
}

namespace uilib {

Result<sdl3::FColor, ScriptError> ParseColor(const Value &v, const char *fn) {
	if (v.IsString()) {
		const String &text = v.AsString();
		const char *s = text.CStr();
		if (s[0] == '#' && (text.GetSize() == 7 || text.GetSize() == 9)) {
			unsigned value = 0;
			float channels[4] = {0, 0, 0, 1};
			const int count = text.GetSize() == 9 ? 4 : 3;
			for (int i = 0; i < count; ++i) {
				if (std::sscanf(s + 1 + i * 2, "%2x", &value) != 1)
					return Err(Fail(String::Format("`%s` : couleur `%s` invalide", fn, s)));
				channels[i] = float(value) / 255.f;
			}
			return Ok(sdl3::FColor{channels[0], channels[1], channels[2], channels[3]});
		}
		return Err(Fail(String::Format("`%s` : couleur `%s` invalide (attendu #rrggbb ou #rrggbbaa)", fn, s)));
	}
	if (v.IsList() && v.AsList() && (v.AsList()->Size() == 3 || v.AsList()->Size() == 4)) {
		const std::vector<Value> items = v.AsList()->Snapshot();
		float c[4] = {0, 0, 0, 1};
		bool bytes = false;
		for (size_t i = 0; i < items.size(); ++i) {
			if (!items[i].IsNumber())
				return Err(Fail(String::Format("`%s` : composante de couleur non numérique", fn)));
			c[i] = items[i].AsFloat();
			bytes = bytes || c[i] > 1.f;
		}
		if (bytes)
			for (float &channel : c)
				channel /= 255.f;
		if (items.size() == 3)
			c[3] = 1.f;
		return Ok(sdl3::FColor{c[0], c[1], c[2], c[3]});
	}
	return Err(Fail(String::Format("`%s` : couleur attendue (\"#rrggbb\" ou [r, g, b]), trouvé `%s`", fn, v.TypeName())));
}

Result<ui::Dimension, ScriptError> ParseDimension(const Value &v, const char *fn) {
	if (v.IsNumber())
		return Ok(ui::Dimension::Px(v.AsFloat()));
	if (v.IsString()) {
		const String &s = v.AsString();
		if (s == "auto")
			return Ok(ui::Dimension::Auto());
		if (s == "grow")
			return Ok(ui::Dimension::Grow(1.f));
		if (s.EndsWith("%")) {
			Option<double> pct = s.Substr(0, s.GetSize() - 1).TryParseDouble();
			if (pct.IsSome())
				return Ok(ui::Dimension::Pct(float(pct.Unwrap()))); // du parent
		}
	}
	return Err(Fail(String::Format("`%s` : dimension attendue (pixels, \"auto\", \"grow\", \"50%%\"), trouvé `%s`", fn,
								   v.ToDisplayString().CStr())));
}

Option<ui::Anchor> ParseAnchor(const String &s) {
	static const std::pair<const char *, ui::Anchor> ANCHORS[] = {
		{"top_left", ui::Anchor::TopLeft},		 {"top", ui::Anchor::Top},		 {"top_right", ui::Anchor::TopRight},
		{"left", ui::Anchor::CenterLeft},		 {"center", ui::Anchor::Center}, {"right", ui::Anchor::CenterRight},
		{"bottom_left", ui::Anchor::BottomLeft}, {"bottom", ui::Anchor::Bottom}, {"bottom_right", ui::Anchor::BottomRight},
	};
	for (const auto &[name, anchor] : ANCHORS)
		if (s == name)
			return Some(anchor);
	return NONE;
}

void PushText(WidgetObject &w) {
	if (!Live(w))
		return;
	ecs::ArchetypeRegistry &r = *w.binding->registry;
	if (auto label = r.GetComponent<ui::UiLabel>(w.entity); label.IsSome())
		label.Unwrap()->text = w.text;
	if (auto button = r.GetComponent<ui::UiButton>(w.entity); button.IsSome())
		button.Unwrap()->text = w.text;
	if (auto input = r.GetComponent<ui::UiInput>(w.entity); input.IsSome())
		input.Unwrap()->text = w.text;
	w.binding->Relayout();
}

void PushValue(WidgetObject &w) {
	if (!Live(w))
		return;
	ecs::ArchetypeRegistry &r = *w.binding->registry;
	if (auto progress = r.GetComponent<ui::UiProgress>(w.entity); progress.IsSome()) {
		progress.Unwrap()->min = float(w.min);
		progress.Unwrap()->max = float(w.max);
		progress.Unwrap()->value = float(w.value);
	}
	if (auto slider = r.GetComponent<ui::UiSlider>(w.entity); slider.IsSome()) {
		slider.Unwrap()->min = float(w.min);
		slider.Unwrap()->max = float(w.max);
		slider.Unwrap()->value = float(w.value);
	}
	if (auto toggle = r.GetComponent<ui::UiToggle>(w.entity); toggle.IsSome())
		toggle.Unwrap()->checked = w.checked;
	if (auto box = r.GetComponent<ui::UiCheckbox>(w.entity); box.IsSome())
		box.Unwrap()->checked = w.checked;
}

void PushVisible(WidgetObject &w) {
	if (!Live(w))
		return;
	ecs::ArchetypeRegistry &r = *w.binding->registry;
	if (!w.visible && !r.HasComponent<ui::UiHidden>(w.entity))
		r.AddComponent(w.entity, ui::UiHidden{});
	else if (w.visible)
		(void)r.RemoveComponent<ui::UiHidden>(w.entity);
	w.binding->Relayout();
}

void PushStyle(WidgetObject &w, const std::function<void(ui::UiStyle &)> &edit) {
	if (!Live(w))
		return;
	ecs::ArchetypeRegistry &r = *w.binding->registry;
	auto style = r.GetOrAddComponent<ui::UiStyle>(w.entity);
	if (style.IsNone())
		return;
	edit(*style.Unwrap());
	if (!r.HasComponent<ui::UiStyleDirty>(w.entity))
		r.AddComponent(w.entity, ui::UiStyleDirty{});
	w.binding->Relayout();
}

void Invoke(Interpreter &vm, const std::weak_ptr<std::atomic<bool>> &alive, const std::shared_ptr<WidgetObject> &w,
		const Value &fn, Option<Value> argument) {
	auto token = alive.lock();
	if (!token || !token->load() || fn.IsNil())
		return;
	Args args{Value::Host(w)};
	if (argument.IsSome())
		args.push_back(argument.Unwrap());
	auto result = vm.CallValue(fn, std::move(args), 0, 0);
	if (result.IsError())
		vm.ReportError(result.Error());
}

Result<Value, ScriptError> Create(Interpreter &vm, const std::shared_ptr<UiState> &state,
		const Spec &spec, std::shared_ptr<WidgetObject> w,
		const MapObject *opts, const char *fn) {
	w->type = state->widgetType;
	w->binding = state->binding;
	w->kind = String(spec.kind);
	std::shared_ptr<WidgetObject> parent;
	if (opts) {
		if (Option<Value> p = opts->Get(String("parent")); p.IsSome() && !p.Value().IsNil()) {
			if (!p.Value().IsHost() || !dynamic_cast<WidgetObject *>(p.Value().AsHost().get()))
				return Err(Fail(String::Format("`%s` : `parent` doit être un widget", fn)));
			parent = std::static_pointer_cast<WidgetObject>(p.Value().AsHost());
		}
		if (Option<Value> n = opts->Get(String("name")); n.IsSome())
			w->name = n.Value().ToDisplayString();
		if (Option<Value> hidden = opts->Get(String("hidden")); hidden.IsSome())
			w->visible = !hidden.Value().IsTruthy();
	}
	const UiBinding &binding = *state->binding;
	const ecs::Entity parentEntity = parent ? parent->entity : binding.Root();
	if (!binding.Alive(parentEntity)) {
		NullBuilder check;
		if (auto error = ApplyOptions(check, opts, fn); error.IsSome())
			return Err(error.Unwrap());
	} else {
		ui::WidgetBuilder builder = spec.make(*binding.factory, *w);
		if (auto error = ApplyOptions(builder, opts, fn); error.IsSome())
			return Err(error.Unwrap());
		if (!w->name.IsEmpty())
			builder.Name(w->name);
		// Rappels de l'interface : état mis à jour tout de suite, script en file.
		std::weak_ptr<WidgetObject> weak = w;
		std::shared_ptr<UiBinding> bindingRef = state->binding;
		auto alive = vm.AliveToken();
		Interpreter *interpreter = &vm;
		auto post = [weak, bindingRef, alive, interpreter](bool click, Option<Value> argument) {
			bindingRef->Post([weak, alive, interpreter, click, argument]() {
				auto widget = weak.lock();
				if (!widget)
					return;
				Value callback;
				{
					std::lock_guard<std::mutex> lock(widget->mutex);
					callback = click ? widget->onClick : widget->onChange;
				}
				Invoke(*interpreter, alive, widget, callback, argument);
			});
		};
		const String &kind = w->kind;
		if (kind == "button")
			builder.OnClick([post]() { post(true, NONE); });
		if (kind == "slider")
			builder.OnChange([weak, post](float v) {
			if (auto widget = weak.lock()) {
				std::lock_guard<std::mutex> lock(widget->mutex);
				widget->value = v;
			}
			post(false, Some(Value::Number(double(v))));
		});
		if (kind == "checkbox" || kind == "toggle")
			builder.OnToggle([weak, post](bool v) {
			if (auto widget = weak.lock()) {
				std::lock_guard<std::mutex> lock(widget->mutex);
				widget->checked = v;
			}
			post(false, Some(Value::Boolean(v)));
		});
		if (kind == "input")
			builder.OnSubmit([weak, post](const String &text) {
			if (auto widget = weak.lock()) {
				std::lock_guard<std::mutex> lock(widget->mutex);
				widget->text = text;
			}
			post(false, Some(Value::Str(text)));
		});
		if (kind == "input")
			builder.OnTextChange([weak](const String &text) {
			if (auto widget = weak.lock()) {
				std::lock_guard<std::mutex> lock(widget->mutex);
				widget->text = text;
			}
		});
		builder.Parent(parentEntity);
		w->entity = builder.Spawn();
		binding.Relayout();
		// Garder le widget tant qu'il est affiché (et oublier ceux qui ne le
		// sont plus : retirés, effacés, calque réinitialisé par l'hôte).
		std::lock_guard<std::mutex> lock(state->mutex);
		std::erase_if(state->onScreen, [](const std::shared_ptr<WidgetObject> &shown) { return !Live(*shown); });
		state->onScreen.push_back(w);
	}
	if (parent) {
		std::lock_guard<std::mutex> lock(parent->mutex);
		parent->children.push_back(w);
	}
	if (!w->name.IsEmpty()) {
		std::lock_guard<std::mutex> lock(state->mutex);
		state->named.push_back(w);
	}
	return Ok(Value::Host(std::move(w)));
}

const MapObject * OptionsOf(Args &args, size_t fixedArgs) {
	if (args.size() > fixedArgs && args.back().IsMap() && args.back().AsMap())
		return args.back().AsMap().get();
	return nullptr;
}

void RemoveTree(WidgetObject &w) {
	if (Live(w)) {
		ui::DespawnTree(*w.binding->registry, w.entity);
		w.binding->Relayout();
	}
	w.entity = ecs::Entity{};
	std::vector<std::weak_ptr<WidgetObject>> children;
	{
		std::lock_guard<std::mutex> lock(w.mutex);
		children.swap(w.children);
	}
	for (auto &weak : children)
		if (auto child = weak.lock())
			child->entity = ecs::Entity{};
}

void DefineWidget(TypeBuilder &builder) {
	using Object = WidgetObject;
	auto locked = [](const HostRef &self, auto fn) {
		Object &w = As<Object>(self);
		std::lock_guard<std::mutex> lock(w.mutex);
		return fn(w);
	};
	builder.Method("set_text", 1, 1, [locked](Interpreter &vm, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		auto text = vm.Stringify(args[0]);
		if (text.IsError())
			return Err(text.Error());
		locked(self, [&](Object &w) {
			w.text = text.Value();
			return 0;
		});
		PushText(As<Object>(self));
		return Ok(Value::Host(self));
	});
	builder.Method("set_value", 1, 1, [locked](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		auto v = lib::ArgFloat(args, 0, "ui.widget.set_value");
		if (v.IsError())
			return Err(v.Error());
		locked(self, [&](Object &w) {
			w.value = v.Value();
			return 0;
		});
		PushValue(As<Object>(self));
		return Ok(Value::Host(self));
	});
	builder.Method("set_range", 2, 2, [locked](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		auto lo = lib::ArgFloat(args, 0, "ui.widget.set_range");
		auto hi = lib::ArgFloat(args, 1, "ui.widget.set_range");
		if (lo.IsError())
			return Err(lo.Error());
		if (hi.IsError())
			return Err(hi.Error());
		locked(self, [&](Object &w) {
			w.min = lo.Value();
			w.max = hi.Value();
			return 0;
		});
		PushValue(As<Object>(self));
		return Ok(Value::Host(self));
	});
	builder.Method("set_checked", 1, 1, [locked](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		locked(self, [&](Object &w) {
			w.checked = args[0].IsTruthy();
			return 0;
		});
		PushValue(As<Object>(self));
		return Ok(Value::Host(self));
	});
	auto visibility = [locked](int mode) {
		return [locked, mode](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
			locked(self, [&](Object &w) {
				w.visible = mode == 0 ? true : mode == 1 ? false : args[0].IsTruthy();
				return 0;
			});
			PushVisible(As<Object>(self));
			return Ok(Value::Host(self));
		};
	};
	builder.Method("show", 0, 0, visibility(0));
	builder.Method("hide", 0, 0, visibility(1));
	builder.Method("set_visible", 1, 1, visibility(2));
	auto styled = [](const char *name, bool color, std::function<void(ui::UiStyle &, sdl3::FColor, float)> edit) {
		return [name, color, edit](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
			sdl3::FColor c{};
			float number = 0.f;
			if (color) {
				auto parsed = ParseColor(args[0], name);
				if (parsed.IsError())
					return Err(parsed.Error());
				c = parsed.Value();
			} else {
				auto parsed = lib::ArgFloat(args, 0, name);
				if (parsed.IsError())
					return Err(parsed.Error());
				number = float(parsed.Value());
			}
			PushStyle(As<Object>(self), [&](ui::UiStyle &style) { edit(style, c, number); });
			return Ok(Value::Host(self));
		};
	};
	builder.Method("set_bg", 1, 1, styled("ui.widget.set_bg", true, [](ui::UiStyle &s, sdl3::FColor c, float) { s.SetBg(c); }));
	builder.Method("set_color", 1, 1,
				   styled("ui.widget.set_color", true, [](ui::UiStyle &s, sdl3::FColor c, float) { s.SetTextColor(c); }));
	builder.Method("set_font_size", 1, 1,
				   styled("ui.widget.set_font_size", false, [](ui::UiStyle &s, sdl3::FColor, float v) { s.SetFontSize(v); }));
	builder.Method("on_click", 1, 1, [locked](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		locked(self, [&](Object &w) {
			w.onClick = args[0];
			return 0;
		});
		return Ok(Value::Host(self));
	});
	builder.Method("on_change", 1, 1, [locked](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		locked(self, [&](Object &w) {
			w.onChange = args[0];
			return 0;
		});
		return Ok(Value::Host(self));
	});
	// Simule un clic / une modification (sans écran : la seule façon de
	// piloter un menu — scénarios de test, démonstrations automatiques).
	builder.Method("click", 0, 0, [locked](Interpreter &vm, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		Value callback = locked(self, [](Object &w) { return w.onClick; });
		if (callback.IsNil())
			return Ok(Value::Nil());
		return vm.CallValue(callback, {Value::Host(self)}, 0, 0);
	});
	builder.Method("change", 1, 1, [locked](Interpreter &vm, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		Value callback = locked(self, [&](Object &w) {
			if (args[0].IsNumber())
				w.value = args[0].AsNumber();
			else if (args[0].IsBoolean())
				w.checked = args[0].AsBoolean();
			else if (args[0].IsString())
				w.text = args[0].AsString();
			return w.onChange;
		});
		PushValue(As<Object>(self));
		PushText(As<Object>(self));
		if (callback.IsNil())
			return Ok(Value::Nil());
		return vm.CallValue(callback, {Value::Host(self), args[0]}, 0, 0);
	});
	// Rattache des widgets existants sous celui-ci.
	builder.Method("add", 1, -1, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		Object &parent = As<Object>(self);
		for (const Value &arg : args) {
			if (!arg.IsHost() || !dynamic_cast<Object *>(arg.AsHost().get()))
				return Err(Fail(String::Format("`ui.widget.add` : widget attendu, trouvé `%s`", arg.TypeName())));
			auto child = std::static_pointer_cast<Object>(arg.AsHost());
			if (Live(parent) && Live(*child)) {
				ui::SetParent(*parent.binding->registry, child->entity, parent.entity);
				parent.binding->Relayout();
			}
			std::lock_guard<std::mutex> lock(parent.mutex);
			parent.children.push_back(child);
		}
		return Ok(Value::Host(self));
	});
	builder.Method("remove", 0, 0, [](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		RemoveTree(As<Object>(self));
		return Ok(Value::Nil());
	});
	builder.Method("clear", 0, 0, [](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		Object &w = As<Object>(self);
		std::vector<std::weak_ptr<Object>> children;
		{
			std::lock_guard<std::mutex> lock(w.mutex);
			children.swap(w.children);
		}
		for (auto &weak : children)
			if (auto child = weak.lock())
				RemoveTree(*child);
		// Sans objet script (enfants créés par l'hôte) : on vide l'entité.
		if (Live(w)) {
			std::vector<ecs::Entity> entities;
			if (auto list = w.binding->registry->GetComponent<ui::UiChildren>(w.entity); list.IsSome())
				entities = list.Unwrap()->list;
			for (ecs::Entity entity : entities)
				ui::DespawnTree(*w.binding->registry, entity);
			if (auto list = w.binding->registry->GetComponent<ui::UiChildren>(w.entity); list.IsSome())
				list.Unwrap()->list.clear();
			w.binding->Relayout();
		}
		return Ok(Value::Host(self));
	});
	builder.Method("children", 0, 0, [locked](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		auto list = std::make_shared<ListObject>();
		locked(self, [&](Object &w) {
			for (auto &weak : w.children)
				if (auto child = weak.lock())
					list->items.push_back(Value::Host(child));
			return 0;
		});
		return Ok(Value::List(std::move(list)));
	});
	// Rectangle à l'écran [x, y, largeur, hauteur] (nil sans écran).
	builder.Method("rect", 0, 0, [](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		const Object &w = As<Object>(self);
		if (!Live(w))
			return Ok(Value::Nil());
		auto computed = w.binding->registry->GetComponent<ui::UiComputed>(w.entity);
		if (computed.IsNone())
			return Ok(Value::Nil());
		const sdl3::FRect r = computed.Unwrap()->screen;
		return Ok(Value::List(std::make_shared<ListObject>(
			std::vector<Value>{Value::Number(r.x), Value::Number(r.y), Value::Number(r.w), Value::Number(r.h)})));
	});

	HostType &type = *builder.type;
	type.mainThreadOnly = true;
	type.get = [](const HostObject &self, const String &name) -> Option<Value> {
		const Object &w = As<Object>(self);
		std::lock_guard<std::mutex> lock(w.mutex);
		if (name == "text")
			return Some(Value::Str(w.text));
		if (name == "value")
			return Some(Value::Number(w.value));
		if (name == "checked")
			return Some(Value::Boolean(w.checked));
		if (name == "visible")
			return Some(Value::Boolean(w.visible));
		if (name == "kind")
			return Some(Value::Str(w.kind));
		if (name == "name")
			return Some(Value::Str(w.name));
		if (name == "on_screen")
			return Some(Value::Boolean(Live(w)));
		return NONE;
	};
	type.set = [](Interpreter &vm, const HostRef &self, const String &name, const Value &value) -> Result<bool, ScriptError> {
		const char *method = name == "text" ? "set_text"
							 : name == "value" ? "set_value"
							 : name == "checked" ? "set_checked"
							 : name == "visible" ? "set_visible"
												 : nullptr;
		if (!method)
			return Ok(false);
		Args args{value};
		auto result = self->type->FindMethod(String(method))->fn(vm, self, args);
		if (result.IsError())
			return Err(result.Error());
		return Ok(true);
	};
	type.display = [](const HostObject &self) {
		const Object &w = As<Object>(self);
		std::lock_guard<std::mutex> lock(w.mutex);
		return w.text.IsEmpty() ? String::Format("<ui.%s>", w.kind.CStr())
								: String::Format("<ui.%s \"%s\">", w.kind.CStr(), w.text.CStr());
	};
	type.items = [](const HostObject &self) {
		const Object &w = As<Object>(self);
		std::lock_guard<std::mutex> lock(w.mutex);
		std::vector<Value> out;
		for (auto &weak : w.children)
			if (auto child = weak.lock())
				out.push_back(Value::Host(child));
		return out;
	};
	// `widget <- valeur` : son texte (ou sa valeur, pour une jauge).
	type.flowIn = [](Interpreter &vm, const HostRef &self, const Value &value) -> Result<Value, ScriptError> {
		const Object &w = As<Object>(self);
		const bool numeric = value.IsNumber() && (w.kind == "progress" || w.kind == "slider");
		Args args{value};
		auto result = self->type->FindMethod(String(numeric ? "set_value" : "set_text"))->fn(vm, self, args);
		if (result.IsError())
			return result;
		return Ok(Value::Host(self));
	};
}

} // namespace uilib

void InstallUiLibrary(Interpreter &vm, std::shared_ptr<UiBinding> binding) {
	using namespace uilib;
	if (!binding)
		binding = std::make_shared<UiBinding>();
	TypeBuilder widget("ui.widget");
	DefineWidget(widget);
	std::shared_ptr<const HostType> widgetType = vm.RegisterHostType(String("ui"), String("widget"), widget.type);
	auto state = std::make_shared<UiState>();
	state->binding = binding;
	state->widgetType = widgetType;
	const String ns("ui");
	constexpr auto MAIN = Interpreter::NativeThread::MAIN;

	// Constructeur générique : `fixed` arguments obligatoires, puis options.
	auto define = [&vm, ns, state](const char *name, int minArity, int maxArity, Spec spec,
								   std::function<Option<ScriptError>(Interpreter &, WidgetObject &, Args &)> init) {
		const String qualified = String::Format("ui.%s", name);
		vm.RegisterNamespacedNative(
			ns, String(name), minArity, maxArity,
			[state, spec, init, qualified, minArity](Interpreter &vm, Args &args) -> Result<Value, ScriptError> {
				const MapObject *opts = OptionsOf(args, size_t(minArity));
				Args fixed(args.begin(), args.end() - (opts ? 1 : 0));
				auto w = std::make_shared<WidgetObject>();
				if (init)
					if (auto error = init(vm, *w, fixed); error.IsSome())
						return Err(error.Unwrap());
				return Create(vm, state, spec, std::move(w), opts, qualified.CStr());
			},
			MAIN);
	};
	auto textArg = [](size_t index) {
		return [index](Interpreter &vm, WidgetObject &w, Args &args) -> Option<ScriptError> {
			if (index < args.size()) {
				auto text = vm.Stringify(args[index]);
				if (text.IsError())
					return Some(text.Error());
				w.text = text.Unwrap();
			}
			return NONE;
		};
	};
	auto callbackArg = [](size_t index, bool click) {
		return [index, click](Interpreter &vm, WidgetObject &w, Args &args) -> Option<ScriptError> {
			if (index < args.size() && !args[index].IsNil()) {
				if (!vm.IsCallableValue(args[index]))
					return Some(Fail(String::Format("ui : fonction de rappel attendue, trouvé `%s`", args[index].TypeName())));
				(click ? w.onClick : w.onChange) = args[index];
			}
			return NONE;
		};
	};

	define("column", 0, 1, Spec{"column", [](ui::UiFactory &f, const WidgetObject &) { return f.Column(); }}, nullptr);
	define("row", 0, 1, Spec{"row", [](ui::UiFactory &f, const WidgetObject &) { return f.Row(); }}, nullptr);
	define("panel", 0, 1, Spec{"panel", [](ui::UiFactory &f, const WidgetObject &) { return f.Panel(); }}, nullptr);
	define("label", 1, 2, Spec{"label", [](ui::UiFactory &f, const WidgetObject &w) { return f.Label(w.text); }},
		   textArg(0));
	define("badge", 1, 2, Spec{"badge", [](ui::UiFactory &f, const WidgetObject &w) { return f.Badge(w.text); }},
		   textArg(0));
	// `ui.button(texte[, fn(bouton)][, options])`
	define("button", 1, 3, Spec{"button", [](ui::UiFactory &f, const WidgetObject &w) { return f.Button(w.text); }},
		   [textArg, callbackArg](Interpreter &vm, WidgetObject &w, Args &args) -> Option<ScriptError> {
			   if (auto error = textArg(0)(vm, w, args); error.IsSome())
				   return error;
			   return callbackArg(1, true)(vm, w, args);
		   });
	// `ui.progress(valeur[, max][, options])` (min 0)
	define("progress", 1, 3,
		   Spec{"progress", [](ui::UiFactory &f, const WidgetObject &w) {
					return f.Progress(float(w.min), float(w.max), float(w.value));
				}},
		   [](Interpreter &, WidgetObject &w, Args &args) -> Option<ScriptError> {
			   w.value = args[0].AsNumber();
			   if (args.size() > 1)
				   w.max = args[1].AsNumber();
			   return NONE;
		   });
	// `ui.slider(min, max, valeur[, fn(widget, valeur)][, options])`
	define("slider", 3, 5,
		   Spec{"slider", [](ui::UiFactory &f, const WidgetObject &w) {
					return f.Slider(float(w.min), float(w.max), float(w.value));
				}},
		   [callbackArg](Interpreter &vm, WidgetObject &w, Args &args) -> Option<ScriptError> {
			   w.min = args[0].AsNumber();
			   w.max = args[1].AsNumber();
			   w.value = args[2].AsNumber();
			   return callbackArg(3, false)(vm, w, args);
		   });
	auto checkable = [callbackArg](Interpreter &vm, WidgetObject &w, Args &args) -> Option<ScriptError> {
		w.checked = !args.empty() && args[0].IsTruthy();
		return callbackArg(1, false)(vm, w, args);
	};
	define("checkbox", 0, 3, Spec{"checkbox", [](ui::UiFactory &f, const WidgetObject &w) { return f.Checkbox(w.checked); }},
		   checkable);
	define("toggle", 0, 3, Spec{"toggle", [](ui::UiFactory &f, const WidgetObject &w) { return f.Toggle(w.checked); }},
		   checkable);
	// `ui.input(indication[, fn(widget, texte) à la validation][, options])`
	define("input", 0, 3,
		   Spec{"input", [](ui::UiFactory &f, const WidgetObject &w) { return f.Input(w.placeholder); }},
		   [callbackArg](Interpreter &vm, WidgetObject &w, Args &args) -> Option<ScriptError> {
			   if (!args.empty())
				   w.placeholder = args[0].ToDisplayString(); // l'indication n'est pas le texte saisi
			   return callbackArg(1, false)(vm, w, args);
		   });
	define("separator", 0, 1, Spec{"separator", [](ui::UiFactory &f, const WidgetObject &) { return f.Separator(); }},
		   nullptr);
	define("spinner", 0, 1, Spec{"spinner", [](ui::UiFactory &f, const WidgetObject &) { return f.Spinner(); }}, nullptr);
	// `ui.image(clé_de_texture, largeur, hauteur[, options])`
	define("image", 3, 4,
		   Spec{"image", [](ui::UiFactory &f, const WidgetObject &w) {
					return f.Image(w.text, float(w.min), float(w.max));
				}},
		   [](Interpreter &, WidgetObject &w, Args &args) -> Option<ScriptError> {
			   w.text = args[0].ToDisplayString();
			   w.min = args[1].AsNumber(); // largeur
			   w.max = args[2].AsNumber(); // hauteur
			   return NONE;
		   });

	vm.RegisterNamespacedNative(ns, String("available"), 0, 0, [binding](Interpreter &, Args &) -> Result<Value, ScriptError> {
		return Ok(Value::Boolean(binding->Live()));
	}, MAIN);
	vm.RegisterNamespacedNative(ns, String("root"), 0, 0, [state](Interpreter &, Args &) -> Result<Value, ScriptError> {
		auto w = std::make_shared<WidgetObject>();
		w->type = state->widgetType;
		w->binding = state->binding;
		w->kind = String("root");
		w->entity = state->binding->Root();
		return Ok(Value::Host(std::move(w)));
	}, MAIN);
	// Retire tout ce que le script a mis dans sa racine.
	vm.RegisterNamespacedNative(ns, String("clear"), 0, 0, [state](Interpreter &, Args &) -> Result<Value, ScriptError> {
		const UiBinding &binding = *state->binding;
		const ecs::Entity root = binding.Root();
		if (binding.Alive(root)) {
			std::vector<ecs::Entity> children;
			if (auto list = binding.registry->GetComponent<ui::UiChildren>(root); list.IsSome())
				children = list.Unwrap()->list;
			for (ecs::Entity child : children)
				ui::DespawnTree(*binding.registry, child);
			if (auto list = binding.registry->GetComponent<ui::UiChildren>(root); list.IsSome())
				list.Unwrap()->list.clear();
			binding.Relayout();
		}
		std::lock_guard<std::mutex> lock(state->mutex);
		state->named.clear();
		state->onScreen.clear();
		return Ok(Value::Nil());
	}, MAIN);
	vm.RegisterNamespacedNative(ns, String("find"), 1, 1, [state](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		const String name = args[0].ToDisplayString();
		std::lock_guard<std::mutex> lock(state->mutex);
		for (auto it = state->named.rbegin(); it != state->named.rend(); ++it)
			if (auto w = it->lock(); w && w->name == name)
				return Ok(Value::Host(std::move(w)));
		return Ok(Value::Nil());
	}, MAIN);
	// Taille de la racine [largeur, hauteur] ([0, 0] sans écran).
	vm.RegisterNamespacedNative(ns, String("size"), 0, 0, [binding](Interpreter &, Args &) -> Result<Value, ScriptError> {
		float w = 0.f, h = 0.f;
		const ecs::Entity root = binding->Root();
		if (binding->Alive(root))
			if (auto computed = binding->registry->GetComponent<ui::UiComputed>(root); computed.IsSome()) {
				w = computed.Unwrap()->screen.w;
				h = computed.Unwrap()->screen.h;
			}
		return Ok(Value::List(std::make_shared<ListObject>(std::vector<Value>{Value::Number(w), Value::Number(h)})));
	}, MAIN);
}

} // namespace data::script
