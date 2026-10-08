// Définitions de runtime2d.hpp
#include "runtime2d.hpp"

namespace game_editor {

namespace script2d {

Value Vec2ToValue(math::FVector2 v) {
	auto list = std::make_shared<data::script::ListObject>();
	list->items.push_back(Value::Number(double(v.x)));
	list->items.push_back(Value::Number(double(v.y)));
	return Value::List(std::move(list));
}

Option<math::FVector2> ToVec2(const Value *value) {
	if (!value || !value->IsList() || !value->AsList() || value->AsList()->items.size() < 2)
		return NONE;
	const std::vector<Value> &items = value->AsList()->items;
	if (!items[0].IsNumber() || !items[1].IsNumber())
		return NONE;
	return Some(math::FVector2{items[0].AsFloat(), items[1].AsFloat()});
}

Result<sdl3::Color, ScriptError> ToColor(const Value &value, const char *fn) {
	auto parsed = data::script::uilib::ParseColor(value, fn);
	if (parsed.IsError())
		return Err(parsed.Error());
	const sdl3::FColor c = parsed.Value();
	auto byte = [](float v) { return uint8_t(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
	return Ok(sdl3::Color{byte(c.r), byte(c.g), byte(c.b), byte(c.a)});
}

Result<std::vector<math::FVector2>, ScriptError> ToPoints(const Value &value, const char *fn) {
	std::vector<math::FVector2> points;
	if (value.IsList() && value.AsList())
		for (const Value &item : value.AsList()->items) {
			Option<math::FVector2> point = ToVec2(&item);
			if (point.IsNone())
				return Err(Interpreter::MakeError(String::Format("`%s` : point [x, y] attendu", fn)));
			points.push_back(point.Unwrap());
		}
	else
		return Err(Interpreter::MakeError(String::Format("`%s` : liste de points attendue", fn)));
	return Ok(std::move(points));
}

Result<float, ScriptError> Number(const std::vector<Value> &args, size_t index, const char *fn) {
	auto value = data::script::detail::ArgNumber(args, index, fn);
	if (value.IsError())
		return Err(value.Error());
	return Ok(float(value.Value()));
}

Result<std::shared_ptr<data::script::MapObject>, ScriptError> Options(const std::vector<Value> &args, size_t index, const char *fn) {
	if (args.size() <= index || args[index].IsNil())
		return Ok(std::make_shared<data::script::MapObject>());
	return data::script::detail::ArgMap(args, index, fn);
}

Option<ScriptError> ApplyStyle(CanvasItemDesc &item, const data::script::MapObject &opts, const char *fn) {
	namespace sd = script_detail;
	item.z = int(sd::FieldFloat(opts, "z", float(item.z)));
	item.filled = sd::FieldBool(opts, "filled", item.filled);
	item.outline = sd::FieldFloat(opts, "outline", item.outline);
	item.centered = sd::FieldBool(opts, "centered", item.centered);
	item.fontSize = sd::FieldFloat(opts, "size", item.fontSize);
	item.flipH = sd::FieldBool(opts, "flip_h", item.flipH);
	item.flipV = sd::FieldBool(opts, "flip_v", item.flipV);
	if (const Value *align = opts.Find(String("align")); align && align->IsString())
		item.align = TextAlign2DFromName(align->AsString());
	for (const char *key : {"color", "outline_color"})
		if (const Value *value = opts.Find(String(key))) {
			auto color = ToColor(*value, fn);
			if (color.IsError())
				return Some(color.Error());
			(String(key) == "color" ? item.color : item.outlineColor) = color.Value();
		}
	return NONE;
}

} // namespace script2d

void Runtime::Install2DApi(data::script::Interpreter &vm) {
	using data::script::Interpreter;
	using data::script::ScriptError;
	using data::script::Value;
	using script2d::ToColor;
	using script2d::Vec2ToValue;
	namespace sd = script_detail;
	using Args = std::vector<Value>;
	Runtime *self = this;

	/// Le nœud nommé en argument 0 (nullptr s'il n'existe pas).
	auto nodeArg = [self](const Args &args, const char *fn) -> Result<scene::Node *, ScriptError> {
		auto name = data::script::detail::ArgString(args, 0, fn);
		if (name.IsError())
			return Err(name.Error());
		return Ok(self->FindNode(self->ResolveId(name.Value())));
	};
	/// Enregistre `ns.name(args)` où args[0] est un nom de nœud : `body`
	/// reçoit le nœud (jamais nul : `nil` est rendu sinon).
	auto onNode = [&vm, nodeArg](const char *ns, const char *name, int minArgs, int maxArgs, auto body) {
		const String fn = String::Format("%s.%s", ns, name);
		vm.RegisterNamespacedNative(String(ns), String(name), minArgs, maxArgs,
								   [nodeArg, body, fn](Interpreter &, Args &args) -> Result<Value, ScriptError> {
									   auto node = nodeArg(args, fn.CStr());
									   if (node.IsError())
										   return Err(node.Error());
									   if (!node.Value())
										   return Ok(Value::Nil());
									   return body(*node.Value(), args, fn.CStr());
								   });
	};
	/// Modifie le CanvasItem d'un nœud (sans historique : appelé à chaque image).
	auto withItem = [](scene::Node &node, const std::function<Option<ScriptError>(CanvasItemDesc &)> &change)
		-> Result<Value, ScriptError> {
		if (!CanvasItemDesc::Has(node))
			return Ok(Value::Boolean(false));
		CanvasItemDesc item = CanvasItemDesc::Read(node);
		if (Option<ScriptError> error = change(item); error.IsSome())
			return Err(error.Unwrap());
		item.Write(node);
		return Ok(Value::Boolean(true));
	};

	// ── node2d.* : transform ──────────────────────────────────────────────────

	onNode("node2d", "position", 1, 1, [](scene::Node &node, Args &, const char *) -> Result<Value, ScriptError> {
		return Ok(Vec2ToValue({node.transform.position.x, node.transform.position.y}));
	});
	onNode("node2d", "set_position", 3, 3, [](scene::Node &node, Args &args, const char *fn) -> Result<Value, ScriptError> {
		auto x = script2d::Number(args, 1, fn), y = script2d::Number(args, 2, fn);
		if (x.IsError())
			return Err(x.Error());
		if (y.IsError())
			return Err(y.Error());
		node.transform.position.x = x.Value();
		node.transform.position.y = y.Value();
		return Ok(Value::Boolean(true));
	});
	onNode("node2d", "move", 3, 3, [](scene::Node &node, Args &args, const char *fn) -> Result<Value, ScriptError> {
		auto dx = script2d::Number(args, 1, fn), dy = script2d::Number(args, 2, fn);
		if (dx.IsError())
			return Err(dx.Error());
		if (dy.IsError())
			return Err(dy.Error());
		node.transform.position.x += dx.Value();
		node.transform.position.y += dy.Value();
		return Ok(Vec2ToValue({node.transform.position.x, node.transform.position.y}));
	});
	onNode("node2d", "rotation", 1, 1, [](scene::Node &node, Args &, const char *) -> Result<Value, ScriptError> {
		return Ok(Value::Number(double(node.transform.EulerDegrees().z)));
	});
	onNode("node2d", "set_rotation", 2, 2, [](scene::Node &node, Args &args, const char *fn) -> Result<Value, ScriptError> {
		auto degrees = script2d::Number(args, 1, fn);
		if (degrees.IsError())
			return Err(degrees.Error());
		node.transform.SetEulerDegrees({0.f, 0.f, degrees.Value()});
		return Ok(Value::Boolean(true));
	});
	onNode("node2d", "scale", 1, 1, [](scene::Node &node, Args &, const char *) -> Result<Value, ScriptError> {
		return Ok(Vec2ToValue({node.transform.scale.x, node.transform.scale.y}));
	});
	onNode("node2d", "set_scale", 2, 3, [](scene::Node &node, Args &args, const char *fn) -> Result<Value, ScriptError> {
		auto sx = script2d::Number(args, 1, fn);
		if (sx.IsError())
			return Err(sx.Error());
		auto sy = args.size() > 2 ? script2d::Number(args, 2, fn) : Result<float, ScriptError>(Ok(sx.Value()));
		if (sy.IsError())
			return Err(sy.Error());
		node.transform.scale.x = sx.Value();
		node.transform.scale.y = sy.Value();
		return Ok(Value::Boolean(true));
	});
	onNode("node2d", "set_visible", 2, 2, [](scene::Node &node, Args &args, const char *) -> Result<Value, ScriptError> {
		node.visible = args[1].IsTruthy();
		return Ok(Value::Boolean(true));
	});
	/// Position dans le monde 2D (parents compris).
	onNode("node2d", "global_position", 1, 1, [self](scene::Node &node, Args &, const char *) -> Result<Value, ScriptError> {
		for (const DrawItem2D &item : self->SceneFrame2D().items)
			if (item.id == node.id)
				return Ok(Vec2ToValue(item.transform.Origin()));
		return Ok(Vec2ToValue({node.transform.position.x, node.transform.position.y}));
	});

	// ── node2d.* : apparence ──────────────────────────────────────────────────

	onNode("node2d", "color", 1, 1, [](scene::Node &node, Args &, const char *) -> Result<Value, ScriptError> {
		if (!CanvasItemDesc::Has(node))
			return Ok(Value::Nil());
		const sdl3::Color c = CanvasItemDesc::Read(node).color;
		auto list = std::make_shared<data::script::ListObject>();
		for (uint8_t channel : {c.r, c.g, c.b, c.a})
			list->items.push_back(Value::Number(double(channel)));
		return Ok(Value::List(std::move(list)));
	});
	onNode("node2d", "set_color", 2, 2, [withItem](scene::Node &node, Args &args, const char *fn) -> Result<Value, ScriptError> {
		auto color = ToColor(args[1], fn);
		if (color.IsError())
			return Err(color.Error());
		return withItem(node, [&](CanvasItemDesc &item) -> Option<ScriptError> {
			item.color = color.Value();
			return NONE;
		});
	});
	onNode("node2d", "text", 1, 1, [](scene::Node &node, Args &, const char *) -> Result<Value, ScriptError> {
		return Ok(CanvasItemDesc::Has(node) ? Value::Str(CanvasItemDesc::Read(node).text) : Value::Nil());
	});
	onNode("node2d", "set_text", 2, 2, [withItem](scene::Node &node, Args &args, const char *) -> Result<Value, ScriptError> {
		const String text = args[1].ToDisplayString();
		return withItem(node, [&](CanvasItemDesc &item) -> Option<ScriptError> {
			item.text = text;
			return NONE;
		});
	});
	onNode("node2d", "size", 1, 1, [](scene::Node &node, Args &, const char *) -> Result<Value, ScriptError> {
		return Ok(CanvasItemDesc::Has(node) ? Vec2ToValue(CanvasItemDesc::Read(node).size) : Value::Nil());
	});
	onNode("node2d", "set_size", 3, 3, [withItem](scene::Node &node, Args &args, const char *fn) -> Result<Value, ScriptError> {
		auto w = script2d::Number(args, 1, fn), h = script2d::Number(args, 2, fn);
		if (w.IsError())
			return Err(w.Error());
		if (h.IsError())
			return Err(h.Error());
		return withItem(node, [&](CanvasItemDesc &item) -> Option<ScriptError> {
			item.size = {std::max(0.f, w.Value()), std::max(0.f, h.Value())};
			return NONE;
		});
	});
	onNode("node2d", "set_texture", 2, 2, [withItem](scene::Node &node, Args &args, const char *fn) -> Result<Value, ScriptError> {
		auto path = data::script::detail::ArgString(args, 1, fn);
		if (path.IsError())
			return Err(path.Error());
		return withItem(node, [&](CanvasItemDesc &item) -> Option<ScriptError> {
			item.texture = path.Value();
			return NONE;
		});
	});
	onNode("node2d", "set_z", 2, 2, [withItem](scene::Node &node, Args &args, const char *fn) -> Result<Value, ScriptError> {
		auto z = script2d::Number(args, 1, fn);
		if (z.IsError())
			return Err(z.Error());
		return withItem(node, [&](CanvasItemDesc &item) -> Option<ScriptError> {
			item.z = int(z.Value());
			return NONE;
		});
	});
	/// `node2d.set_style(nom, {color, outline, outline_color, filled, z, size…})`.
	onNode("node2d", "set_style", 2, 2, [withItem](scene::Node &node, Args &args, const char *fn) -> Result<Value, ScriptError> {
		auto opts = data::script::detail::ArgMap(args, 1, fn);
		if (opts.IsError())
			return Err(opts.Error());
		return withItem(node, [&](CanvasItemDesc &item) { return script2d::ApplyStyle(item, *opts.Value(), fn); });
	});

	// ── node2d.* : création, sélection, collisions ─────────────────────────

	/// `node2d.spawn({name, kind, pos, size, color, text, texture, points,
	/// parent, rotation, scale, z, tag, …})` → nom du nœud créé.
	vm.RegisterNamespacedNative(
		String("node2d"), String("spawn"), 1, 1, [self](Interpreter &, Args &args) -> Result<Value, ScriptError> {
			const char *fn = "node2d.spawn";
			auto table = data::script::detail::ArgMap(args, 0, fn);
			if (table.IsError())
				return Err(table.Error());
			const data::script::MapObject &map = *table.Value();
			const String kindName = sd::FieldString(map, "kind", "rect");
			Option<CanvasItemKind> kind = CanvasItemKindFromName(kindName);
			if (kindName != "node" && kindName != "layer" && kind.IsNone())
				return Err(Interpreter::MakeError(String::Format(
					"`%s` : kind `%s` inconnu (rect, circle, polygon, line, sprite, text, node, layer)", fn,
					kindName.CStr())));

			NodeDesc object = NodeDesc::Group(sd::FieldString(map, "name", "Objet 2D"));
			object.parent = sd::FieldString(map, "parent", "");
			object.tag = sd::FieldString(map, "tag", "");
			object.visible = sd::FieldBool(map, "visible", true);
			const math::FVector2 pos = script2d::ToVec2(map.Find(String("pos"))).UnwrapOr(math::FVector2{});
			const math::FVector2 scale =
				script2d::ToVec2(map.Find(String("scale"))).UnwrapOr(math::FVector2{1.f, 1.f});
			object.transform.position = {pos.x, pos.y, 0.f};
			object.transform.SetEulerDegrees({0.f, 0.f, sd::FieldFloat(map, "rotation", 0.f)});
			object.transform.scale = {scale.x, scale.y, 1.f};
			if (kindName == "node" || kindName == "layer") {
				object.type = String(kindName == "node" ? node_kind::NODE2D : node_kind::CANVAS_LAYER);
			} else {
				CanvasItemDesc item;
				item.kind = kind.Unwrap();
				item.size = script2d::ToVec2(map.Find(String("size"))).UnwrapOr(item.size);
				if (const Value *radius = map.Find(String("radius")); radius && radius->IsNumber())
					item.size = {radius->AsFloat() * 2.f, radius->AsFloat() * 2.f};
				item.text = sd::FieldString(map, "text", "");
				item.texture = sd::FieldString(map, "texture", "");
				item.outline = item.kind == CanvasItemKind::LINE ? 3.f : 0.f;
				if (const Value *points = map.Find(String("points"))) {
					auto parsed = script2d::ToPoints(*points, fn);
					if (parsed.IsError())
						return Err(parsed.Error());
					item.points = parsed.Value();
				}
				if (Option<ScriptError> error = script2d::ApplyStyle(item, map, fn); error.IsSome())
					return Err(error.Unwrap());
				object.type = String(item.kind == CanvasItemKind::SPRITE ? node_kind::SPRITE2D
									 : item.kind == CanvasItemKind::TEXT ? node_kind::LABEL2D
																		 : node_kind::SHAPE2D);
				scene::Node scratch;
				item.Write(scratch);
				object.components.push_back(scratch.components.front());
			}
			Option<String> name = self->SpawnNamedNode(std::move(object));
			return Ok(name.IsSome() ? Value::Str(name.Unwrap()) : Value::Nil());
		});

	vm.RegisterNamespacedNative(String("node2d"), String("remove"), 1, 1,
							   [self](Interpreter &, Args &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "node2d.remove");
								   if (name.IsError())
									   return Err(name.Error());
								   return Ok(Value::Boolean(self->RemoveNode(name.Value())));
							   });

	/// Élément 2D le plus en avant au point (x, y) du MONDE 2D (ou de l'écran
	/// de référence avec `"screen"`) → son nom, ou nil.
	vm.RegisterNamespacedNative(
		String("node2d"), String("at"), 2, 3, [self](Interpreter &, Args &args) -> Result<Value, ScriptError> {
			const char *fn = "node2d.at";
			auto x = script2d::Number(args, 0, fn), y = script2d::Number(args, 1, fn);
			if (x.IsError())
				return Err(x.Error());
			if (y.IsError())
				return Err(y.Error());
			const Space2D space =
				args.size() > 2 && args[2].IsString() && args[2].AsString() == "screen" ? Space2D::SCREEN : Space2D::WORLD;
			const Canvas2DFrame frame = self->SceneFrame2D();
			for (size_t i = frame.items.size(); i-- > 0;) {
				const DrawItem2D &item = frame.items[i];
				if (item.space != space)
					continue;
				Option<Transform2D> inverse = item.transform.Inverse();
				if (inverse.IsSome() &&
					HitsLocal(item.item, inverse.Value().Apply({x.Value(), y.Value()}), 0.f, self->m_textMeasure))
					if (const scene::Node *node = self->FindNode(item.id))
						return Ok(Value::Str(node->name));
			}
			return Ok(Value::Nil());
		});

	/// Les deux nœuds se recouvrent-ils (boîtes orientées) ?
	vm.RegisterNamespacedNative(
		String("node2d"), String("overlaps"), 2, 2, [self](Interpreter &, Args &args) -> Result<Value, ScriptError> {
			auto a = data::script::detail::ArgString(args, 0, "node2d.overlaps");
			if (a.IsError())
				return Err(a.Error());
			auto b = data::script::detail::ArgString(args, 1, "node2d.overlaps");
			if (b.IsError())
				return Err(b.Error());
			const scene::NodeId ida = self->ResolveId(a.Value()), idb = self->ResolveId(b.Value());
			const Canvas2DFrame frame = self->SceneFrame2D();
			const DrawItem2D *first = nullptr, *second = nullptr;
			for (const DrawItem2D &item : frame.items) {
				if (item.id == ida)
					first = &item;
				if (item.id == idb)
					second = &item;
			}
			if (!first || !second || first->space != second->space)
				return Ok(Value::Boolean(false));
			return Ok(Value::Boolean(Overlaps(*first, *second, self->m_textMeasure)));
		});

	/// Boîte englobante dans le monde 2D : [x, y, largeur, hauteur].
	onNode("node2d", "bounds", 1, 1, [self](scene::Node &node, Args &, const char *) -> Result<Value, ScriptError> {
		for (const DrawItem2D &item : self->SceneFrame2D().items) {
			if (item.id != node.id)
				continue;
			const Bounds2D local = LocalBounds(item.item, self->m_textMeasure);
			float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
			for (math::FVector2 corner : {local.min, math::FVector2{local.max.x, local.min.y}, local.max,
										 math::FVector2{local.min.x, local.max.y}}) {
				const math::FVector2 p = item.transform.Apply(corner);
				minX = std::min(minX, p.x), minY = std::min(minY, p.y);
				maxX = std::max(maxX, p.x), maxY = std::max(maxY, p.y);
			}
			auto list = std::make_shared<data::script::ListObject>();
			for (float v : {minX, minY, maxX - minX, maxY - minY})
				list->items.push_back(Value::Number(double(v)));
			return Ok(Value::List(std::move(list)));
		}
		return Ok(Value::Nil());
	});

	// ── canvas.* ─────────────────────────────────────────────────────────────

	vm.RegisterNamespacedNative(String("canvas"), String("size"), 0, 0,
							   [self](Interpreter &, Args &) -> Result<Value, ScriptError> {
								   return Ok(Vec2ToValue(self->Reference2D()));
							   });
	/// Souris en coordonnées de l'écran de référence (nil hors de la vue).
	vm.RegisterNamespacedNative(String("canvas"), String("mouse"), 0, 0,
							   [self](Interpreter &, Args &) -> Result<Value, ScriptError> {
								   float x = 0.f, y = 0.f;
								   SDL_GetMouseState(&x, &y);
								   Option<math::FVector2> p = self->PointerTo2D(x, y, Space2D::SCREEN);
								   return Ok(p.IsSome() ? Vec2ToValue(p.Unwrap()) : Value::Nil());
							   });
	/// Souris dans le monde 2D (caméra 2D comprise).
	vm.RegisterNamespacedNative(String("canvas"), String("mouse_world"), 0, 0,
							   [self](Interpreter &, Args &) -> Result<Value, ScriptError> {
								   float x = 0.f, y = 0.f;
								   SDL_GetMouseState(&x, &y);
								   Option<math::FVector2> p = self->PointerTo2D(x, y, Space2D::WORLD);
								   return Ok(p.IsSome() ? Vec2ToValue(p.Unwrap()) : Value::Nil());
							   });
	/// Bouton (1 gauche, 2 milieu, 3 droit) enfoncé / enfoncé À CETTE image.
	auto buttonMask = [](const Args &args) -> uint32_t {
		const int button = args.empty() || !args[0].IsNumber() ? 1 : int(args[0].AsNumber());
		return button >= 1 && button <= 5 ? (1u << (button - 1)) : 0u;
	};
	vm.RegisterNamespacedNative(String("canvas"), String("mouse_down"), 0, 1,
							   [self, buttonMask](Interpreter &, Args &args) -> Result<Value, ScriptError> {
								   return Ok(Value::Boolean((self->m_mouseButtons & buttonMask(args)) != 0));
							   });
	vm.RegisterNamespacedNative(String("canvas"), String("mouse_pressed"), 0, 1,
							   [self, buttonMask](Interpreter &, Args &args) -> Result<Value, ScriptError> {
								   const uint32_t mask = buttonMask(args);
								   return Ok(Value::Boolean((self->m_mouseButtons & mask) != 0 &&
															(self->m_previousMouseButtons & mask) == 0));
							   });
	/// Caméra 2D courante : [x, y, zoom], ou nil.
	vm.RegisterNamespacedNative(String("canvas"), String("camera"), 0, 0,
							   [self](Interpreter &, Args &) -> Result<Value, ScriptError> {
								   const Canvas2DFrame frame = self->SceneFrame2D();
								   if (frame.camera.IsNone())
									   return Ok(Value::Nil());
								   auto list = std::make_shared<data::script::ListObject>();
								   const Camera2DState &camera = frame.camera.Value();
								   for (float v : {camera.position.x, camera.position.y, camera.zoom})
									   list->items.push_back(Value::Number(double(v)));
								   return Ok(Value::List(std::move(list)));
							   });
	/// Fait de la caméra 2D nommée la caméra du jeu (les autres cessent de l'être).
	vm.RegisterNamespacedNative(
		String("canvas"), String("set_camera"), 1, 1, [self](Interpreter &, Args &args) -> Result<Value, ScriptError> {
			auto name = data::script::detail::ArgString(args, 0, "canvas.set_camera");
			if (name.IsError())
				return Err(name.Error());
			SceneDesc *scene = self->ActiveScene();
			const scene::NodeId target = self->ResolveId(name.Value());
			const scene::Node *wanted = self->FindNode(target);
			if (!scene || !wanted || !Camera2DDesc::Has(*wanted))
				return Ok(Value::Boolean(false));
			for (scene::NodeId id : scene->Nodes())
				if (scene::Node *node = scene->tree.Get(id); node && Camera2DDesc::Has(*node)) {
					Camera2DDesc camera = Camera2DDesc::Read(*node);
					camera.current = id == target;
					camera.Write(*node);
				}
			return Ok(Value::Boolean(true));
		});
	vm.RegisterNamespacedNative(String("canvas"), String("set_background"), 1, 1,
							   [self](Interpreter &, Args &args) -> Result<Value, ScriptError> {
								   auto color = ToColor(args[0], "canvas.set_background");
								   if (color.IsError())
									   return Err(color.Error());
								   if (SceneDesc *scene = self->ActiveScene())
									   scene->canvas.background = color.Value();
								   return Ok(Value::Nil());
							   });

	// ── draw2d.* : dessin immédiat ──────────────────────────────────────────

	/// Ajoute un dessin pour l'image en cours ; `opts` : space, z, rotation,
	/// color, outline, outline_color, filled, centered, size, align.
	auto draw = [self](CanvasItemDesc item, math::FVector2 at, const data::script::MapObject &opts,
					   const char *fn) -> Result<Value, ScriptError> {
		item.z = 100; // devant les nœuds par défaut
		if (Option<ScriptError> error = script2d::ApplyStyle(item, opts, fn); error.IsSome())
			return Err(error.Unwrap());
		const String space = sd::FieldString(opts, "space", "screen");
		if (space != "screen" && space != "world")
			return Err(Interpreter::MakeError(String::Format("`%s` : space `%s` inconnu (screen, world)", fn,
															 space.CStr())));
		DrawItem2D drawing;
		drawing.item = std::move(item);
		drawing.space = space == "world" ? Space2D::WORLD : Space2D::SCREEN;
		drawing.transform = Transform2D::Translate(at) * Transform2D::Rotate(sd::FieldFloat(opts, "rotation", 0.f));
		self->m_draw2d.push_back(std::move(drawing));
		return Ok(Value::Nil());
	};
	/// Lit `count` nombres à partir de l'argument `first`.
	auto numbers = [](const Args &args, size_t first, size_t count, const char *fn) -> Result<std::vector<float>, ScriptError> {
		std::vector<float> out;
		for (size_t i = 0; i < count; ++i) {
			auto v = script2d::Number(args, first + i, fn);
			if (v.IsError())
				return Err(v.Error());
			out.push_back(v.Value());
		}
		return Ok(std::move(out));
	};
	/// Couleur obligatoire en position `index`, puis options facultatives.
	auto colorAndOptions = [](const Args &args, size_t index, CanvasItemDesc &item, const char *fn)
		-> Result<std::shared_ptr<data::script::MapObject>, ScriptError> {
		if (args.size() <= index)
			return Err(Interpreter::MakeError(String::Format("`%s` : couleur attendue", fn)));
		auto color = ToColor(args[index], fn);
		if (color.IsError())
			return Err(color.Error());
		item.color = color.Value();
		return script2d::Options(args, index + 1, fn);
	};

	vm.RegisterNamespacedNative(String("draw2d"), String("rect"), 5, 6,
							   [draw, numbers, colorAndOptions](Interpreter &, Args &args) -> Result<Value, ScriptError> {
								   const char *fn = "draw2d.rect";
								   auto v = numbers(args, 0, 4, fn);
								   if (v.IsError())
									   return Err(v.Error());
								   CanvasItemDesc item;
								   item.centered = false;
								   item.size = {v.Value()[2], v.Value()[3]};
								   auto opts = colorAndOptions(args, 4, item, fn);
								   if (opts.IsError())
									   return Err(opts.Error());
								   return draw(std::move(item), {v.Value()[0], v.Value()[1]}, *opts.Value(), fn);
							   });
	vm.RegisterNamespacedNative(String("draw2d"), String("circle"), 4, 5,
							   [draw, numbers, colorAndOptions](Interpreter &, Args &args) -> Result<Value, ScriptError> {
								   const char *fn = "draw2d.circle";
								   auto v = numbers(args, 0, 3, fn);
								   if (v.IsError())
									   return Err(v.Error());
								   CanvasItemDesc item;
								   item.kind = CanvasItemKind::CIRCLE;
								   item.size = {v.Value()[2] * 2.f, v.Value()[2] * 2.f};
								   auto opts = colorAndOptions(args, 3, item, fn);
								   if (opts.IsError())
									   return Err(opts.Error());
								   return draw(std::move(item), {v.Value()[0], v.Value()[1]}, *opts.Value(), fn);
							   });
	/// `draw2d.line(x1, y1, x2, y2, couleur[, {width}])`.
	vm.RegisterNamespacedNative(
		String("draw2d"), String("line"), 5, 6,
		[draw, numbers, colorAndOptions](Interpreter &, Args &args) -> Result<Value, ScriptError> {
			const char *fn = "draw2d.line";
			auto v = numbers(args, 0, 4, fn);
			if (v.IsError())
				return Err(v.Error());
			CanvasItemDesc item;
			item.kind = CanvasItemKind::LINE;
			item.points = {{0.f, 0.f}, {v.Value()[2] - v.Value()[0], v.Value()[3] - v.Value()[1]}};
			auto opts = colorAndOptions(args, 4, item, fn);
			if (opts.IsError())
				return Err(opts.Error());
			item.outline = script_detail::FieldFloat(*opts.Value(), "width", 2.f);
			return draw(std::move(item), {v.Value()[0], v.Value()[1]}, *opts.Value(), fn);
		});
	/// `draw2d.polygon(points, couleur[, opts])` / `draw2d.polyline(…)`.
	for (const char *name : {"polygon", "polyline"}) {
		const bool closed = String(name) == "polygon";
		vm.RegisterNamespacedNative(
			String("draw2d"), String(name), 2, 3,
			[draw, colorAndOptions, closed, name](Interpreter &, Args &args) -> Result<Value, ScriptError> {
				const String fn = String::Format("draw2d.%s", name);
				auto points = script2d::ToPoints(args[0], fn.CStr());
				if (points.IsError())
					return Err(points.Error());
				CanvasItemDesc item;
				item.kind = closed ? CanvasItemKind::POLYGON : CanvasItemKind::LINE;
				item.points = points.Value();
				auto opts = colorAndOptions(args, 1, item, fn.CStr());
				if (opts.IsError())
					return Err(opts.Error());
				if (!closed)
					item.outline = script_detail::FieldFloat(*opts.Value(), "width", 2.f);
				return draw(std::move(item), {0.f, 0.f}, *opts.Value(), fn.CStr());
			});
	}
	/// `draw2d.text(texte, x, y[, {size, color, align, centered}])` — ancré en
	/// haut à gauche par défaut.
	vm.RegisterNamespacedNative(String("draw2d"), String("text"), 3, 4,
							   [draw, numbers](Interpreter &, Args &args) -> Result<Value, ScriptError> {
								   const char *fn = "draw2d.text";
								   auto v = numbers(args, 1, 2, fn);
								   if (v.IsError())
									   return Err(v.Error());
								   auto opts = script2d::Options(args, 3, fn);
								   if (opts.IsError())
									   return Err(opts.Error());
								   CanvasItemDesc item;
								   item.kind = CanvasItemKind::TEXT;
								   item.text = args[0].ToDisplayString();
								   item.align = TextAlign2D::LEFT;
								   item.centered = false;
								   item.fontSize = 20.f;
								   return draw(std::move(item), {v.Value()[0], v.Value()[1]}, *opts.Value(), fn);
							   });
	/// `draw2d.sprite(image, x, y, largeur, hauteur[, {color (teinte), rotation…}])`.
	vm.RegisterNamespacedNative(
		String("draw2d"), String("sprite"), 5, 6, [draw, numbers](Interpreter &, Args &args) -> Result<Value, ScriptError> {
			const char *fn = "draw2d.sprite";
			auto path = data::script::detail::ArgString(args, 0, fn);
			if (path.IsError())
				return Err(path.Error());
			auto v = numbers(args, 1, 4, fn);
			if (v.IsError())
				return Err(v.Error());
			auto opts = script2d::Options(args, 5, fn);
			if (opts.IsError())
				return Err(opts.Error());
			CanvasItemDesc item;
			item.kind = CanvasItemKind::SPRITE;
			item.texture = path.Value();
			item.centered = false;
			item.size = {v.Value()[2], v.Value()[3]};
			return draw(std::move(item), {v.Value()[0], v.Value()[1]}, *opts.Value(), fn);
		});
}

} // namespace game_editor
