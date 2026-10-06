// Définitions de project.hpp
#include "project.hpp"

namespace game_editor {

const char * ShapeKindName(ShapeKind kind) noexcept {
	switch (kind) {
		case ShapeKind::BOX:
			return "box";
		case ShapeKind::SPHERE:
			return "sphere";
		case ShapeKind::CYLINDER:
			return "cylinder";
		case ShapeKind::CONE:
			return "cone";
		case ShapeKind::TORUS:
			return "torus";
		case ShapeKind::PLANE:
			return "plane";
		case ShapeKind::ICOSAHEDRON:
			return "icosahedron";
		case ShapeKind::TORUS_KNOT:
			return "torus_knot";
		case ShapeKind::PORTAL_QUAD:
			return "portal";
		case ShapeKind::MODEL:
			return "model";
	}
	return "box";
}

Option<ShapeKind> ShapeKindFromName(const String &name) {
	struct Entry {
		const char *name;
		ShapeKind kind;
	};
	static constexpr Entry TABLE[] = {
		{"box", ShapeKind::BOX},           {"cube", ShapeKind::BOX},
		{"sphere", ShapeKind::SPHERE},     {"cylinder", ShapeKind::CYLINDER},
		{"cone", ShapeKind::CONE},         {"torus", ShapeKind::TORUS},
		{"plane", ShapeKind::PLANE},       {"icosahedron", ShapeKind::ICOSAHEDRON},
		{"torus_knot", ShapeKind::TORUS_KNOT}, {"portal", ShapeKind::PORTAL_QUAD},
		{"model", ShapeKind::MODEL},           {"gltf", ShapeKind::MODEL},
	};
	String key = name.ToLower();
	for (const Entry &entry : TABLE)
		if (key == entry.name)
			return Some(entry.kind);
	return NONE;
}

const char * MaterialKindName(MaterialKind kind) noexcept {
	switch (kind) {
		case MaterialKind::PLASTIC:
			return "plastic";
		case MaterialKind::METAL:
			return "metal";
		case MaterialKind::WOOD:
			return "wood";
		case MaterialKind::PBR:
			return "pbr";
		case MaterialKind::UNLIT:
			return "unlit";
		case MaterialKind::BASIC:
			return "basic";
	}
	return "plastic";
}

Option<MaterialKind> MaterialKindFromName(const String &name) {
	struct Entry {
		const char *name;
		MaterialKind kind;
	};
	static constexpr Entry TABLE[] = {
		{"plastic", MaterialKind::PLASTIC}, {"metal", MaterialKind::METAL}, {"wood", MaterialKind::WOOD},
		{"pbr", MaterialKind::PBR},         {"unlit", MaterialKind::UNLIT}, {"basic", MaterialKind::BASIC},
	};
	String key = name.ToLower();
	for (const Entry &entry : TABLE)
		if (key == entry.name)
			return Some(entry.kind);
	return NONE;
}

const char * BodyKindName(BodyKind kind) noexcept {
	switch (kind) {
		case BodyKind::NONE:
			return "none";
		case BodyKind::STATIC:
			return "static";
		case BodyKind::DYNAMIC:
			return "dynamic";
	}
	return "none";
}

Option<BodyKind> BodyKindFromName(const String &name) {
	String key = name.ToLower();
	if (key == "none")
		return Some(BodyKind::NONE);
	if (key == "static")
		return Some(BodyKind::STATIC);
	if (key == "dynamic")
		return Some(BodyKind::DYNAMIC);
	return NONE;
}

const char * ColliderKindName(ColliderKind kind) noexcept {
	switch (kind) {
		case ColliderKind::BOX:
			return "box";
		case ColliderKind::SPHERE:
			return "sphere";
		case ColliderKind::CAPSULE:
			return "capsule";
	}
	return "box";
}

Option<ColliderKind> ColliderKindFromName(const String &name) {
	String key = name.ToLower();
	if (key == "box")
		return Some(ColliderKind::BOX);
	if (key == "sphere")
		return Some(ColliderKind::SPHERE);
	if (key == "capsule")
		return Some(ColliderKind::CAPSULE);
	return NONE;
}

namespace json {

float Float(const data::NodePtr &node, float fallback) noexcept {
	if (!node)
		return fallback;
	if (node->IsInt())
		return float(node->intValue);
	if (node->IsFloat())
		return float(node->floatValue);
	return fallback;
}

int Int(const data::NodePtr &node, int fallback) noexcept {
	if (!node)
		return fallback;
	if (node->IsInt())
		return int(node->intValue);
	if (node->IsFloat())
		return int(node->floatValue);
	return fallback;
}

bool Bool(const data::NodePtr &node, bool fallback) noexcept {
	return (node && node->IsBool()) ? node->boolValue : fallback;
}

String Str(const data::NodePtr &node, const char *fallback) {
	return (node && node->IsString()) ? node->stringValue : String(fallback);
}

data::NodePtr FromVec2(const math::FVector2 &v) {
	auto array = data::Node::MakeArray();
	array->Push(data::Node::MakeFloat(double(v.x)));
	array->Push(data::Node::MakeFloat(double(v.y)));
	return array;
}

math::FVector2 ToVec2(const data::NodePtr &node, math::FVector2 fallback) {
	if (!node || !node->IsArray() || node->GetSize() < 2)
		return fallback;
	return {Float(node->At(0)), Float(node->At(1))};
}

data::NodePtr FromVec3(const math::FVector3 &v) {
	auto array = data::Node::MakeArray();
	array->Push(data::Node::MakeFloat(double(v.x)));
	array->Push(data::Node::MakeFloat(double(v.y)));
	array->Push(data::Node::MakeFloat(double(v.z)));
	return array;
}

math::FVector3 ToVec3(const data::NodePtr &node, math::FVector3 fallback) {
	if (!node || !node->IsArray() || node->GetSize() < 3)
		return fallback;
	return {Float(node->At(0)), Float(node->At(1)), Float(node->At(2))};
}

data::NodePtr FromVec4(const math::FVector4 &v) {
	auto array = data::Node::MakeArray();
	array->Push(data::Node::MakeFloat(double(v.x)));
	array->Push(data::Node::MakeFloat(double(v.y)));
	array->Push(data::Node::MakeFloat(double(v.z)));
	array->Push(data::Node::MakeFloat(double(v.w)));
	return array;
}

math::FVector4 ToVec4(const data::NodePtr &node, math::FVector4 fallback) {
	if (!node || !node->IsArray() || node->GetSize() < 4)
		return fallback;
	return {Float(node->At(0)), Float(node->At(1)), Float(node->At(2)), Float(node->At(3))};
}

data::NodePtr FromColor(const sdl3::Color &c) {
	auto array = data::Node::MakeArray();
	array->Push(data::Node::MakeInt(c.r));
	array->Push(data::Node::MakeInt(c.g));
	array->Push(data::Node::MakeInt(c.b));
	return array;
}

sdl3::Color ToColor(const data::NodePtr &node, sdl3::Color fallback) {
	if (!node || !node->IsArray() || node->GetSize() < 3)
		return fallback;
	auto channel = [](const data::NodePtr &n) -> uint8_t {
		int v = Int(n, 255);
		return uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v));
	};
	return sdl3::Color{channel(node->At(0)), channel(node->At(1)), channel(node->At(2)), 255};
}

} // namespace json

bool IsBuiltinComponent(const String &type) {
	return type == component::VISUAL || type == component::BODY || type == component::LIGHT ||
		   type == component::CAMERA || type == component::TRIGGER || type == component::SCRIPT;
}

const scene::PropertyValue * ComponentProp(const scene::Node &node, const char *componentType, const char *key) {
	const scene::Component *component = node.FindComponent(String(componentType));
	return component ? component->props.Find(String(key)) : nullptr;
}

String TagOf(const scene::Node &node) {
	const scene::PropertyValue *tag = node.Get(String(PROP_TAG));
	return tag ? tag->AsString() : String();
}

void SetTag(scene::Node &node, const String &tag) {
	if (tag.IsEmpty())
		node.properties.Remove(String(PROP_TAG));
	else
		node.Set(String(PROP_TAG), scene::PropertyValue::Str(tag));
}

// ── MaterialDesc ─────────────────────────────────────────────────────────────

data::NodePtr MaterialDesc::ToJson() const {
	auto node = data::Node::MakeObject();
	node->Set("kind", data::Node::MakeString(MaterialKindName(kind)));
	node->Set("base_color", json::FromColor(baseColor));
	node->Set("metallic", data::Node::MakeFloat(double(metallic)));
	node->Set("roughness", data::Node::MakeFloat(double(roughness)));
	node->Set("double_sided", data::Node::MakeBool(doubleSided));
	node->Set("wireframe", data::Node::MakeBool(wireframe));
	return node;
}

MaterialDesc MaterialDesc::FromJson(const data::NodePtr &node) {
	MaterialDesc desc;
	if (!node || !node->IsObject())
		return desc;
	desc.kind = MaterialKindFromName(json::Str(node->Get("kind"), "plastic")).UnwrapOr(MaterialKind::PLASTIC);
	desc.baseColor = json::ToColor(node->Get("base_color"), desc.baseColor);
	desc.metallic = json::Float(node->Get("metallic"), 0.f);
	desc.roughness = json::Float(node->Get("roughness"), 0.5f);
	desc.doubleSided = json::Bool(node->Get("double_sided"), false);
	desc.wireframe = json::Bool(node->Get("wireframe"), false);
	return desc;
}

// ── PhysicsDesc ──────────────────────────────────────────────────────────────

bool PhysicsDesc::Has(const scene::Node &node) noexcept {
	return node.HasComponent(String(component::BODY));
}

PhysicsDesc PhysicsDesc::Read(const scene::Node &node) {
	PhysicsDesc desc;
	const scene::Component *component = node.FindComponent(String(component::BODY));
	if (!component)
		return desc;
	const scene::PropertyMap &props = component->props;
	auto str = [&props](const char *key, const char *fallback) -> String {
		const scene::PropertyValue *value = props.Find(String(key));
		return value ? value->AsString(fallback) : String(fallback);
	};
	auto number = [&props](const char *key, float fallback) -> float {
		const scene::PropertyValue *value = props.Find(String(key));
		return value ? value->AsFloat(fallback) : fallback;
	};
	desc.body = BodyKindFromName(str("body", "none")).UnwrapOr(BodyKind::NONE);
	desc.collider = ColliderKindFromName(str("collider", "box")).UnwrapOr(ColliderKind::BOX);
	if (const scene::PropertyValue *extents = props.Find(String("half_extents")))
		desc.halfExtents = extents->AsVec3(desc.halfExtents);
	desc.mass = number("mass", 1.f);
	desc.restitution = number("restitution", 0.3f);
	desc.friction = number("friction", 0.5f);
	return desc;
}

void PhysicsDesc::Write(scene::Node &node) const {
	if (body == BodyKind::NONE && *this == PhysicsDesc{}) {
		(void)node.RemoveComponent(String(component::BODY));
		return;
	}
	scene::Component component;
	component.type = String(component::BODY);
	component.props.Set(String("body"), scene::PropertyValue::Str(String(BodyKindName(body))));
	component.props.Set(String("collider"), scene::PropertyValue::Str(String(ColliderKindName(collider))));
	component.props.Set(String("half_extents"), scene::PropertyValue::Vec3(halfExtents));
	component.props.Set(String("mass"), scene::PropertyValue::Float(double(mass)));
	component.props.Set(String("restitution"), scene::PropertyValue::Float(double(restitution)));
	component.props.Set(String("friction"), scene::PropertyValue::Float(double(friction)));
	node.SetComponent(std::move(component));
}

data::NodePtr PhysicsDesc::ToJson() const {
	auto node = data::Node::MakeObject();
	node->Set("body", data::Node::MakeString(BodyKindName(body)));
	node->Set("collider", data::Node::MakeString(ColliderKindName(collider)));
	node->Set("half_extents", json::FromVec3(halfExtents));
	node->Set("mass", data::Node::MakeFloat(double(mass)));
	node->Set("restitution", data::Node::MakeFloat(double(restitution)));
	node->Set("friction", data::Node::MakeFloat(double(friction)));
	return node;
}

PhysicsDesc PhysicsDesc::FromJson(const data::NodePtr &node) {
	PhysicsDesc desc;
	if (!node || !node->IsObject())
		return desc;
	desc.body = BodyKindFromName(json::Str(node->Get("body"), "none")).UnwrapOr(BodyKind::NONE);
	desc.collider = ColliderKindFromName(json::Str(node->Get("collider"), "box")).UnwrapOr(ColliderKind::BOX);
	desc.halfExtents = json::ToVec3(node->Get("half_extents"), math::FVector3{0.5f, 0.5f, 0.5f});
	desc.mass = json::Float(node->Get("mass"), 1.f);
	desc.restitution = json::Float(node->Get("restitution"), 0.3f);
	desc.friction = json::Float(node->Get("friction"), 0.5f);
	return desc;
}

// ── VisualDesc ───────────────────────────────────────────────────────────────

bool VisualDesc::Has(const scene::Node &node) noexcept {
	return node.HasComponent(String(component::VISUAL));
}

VisualDesc VisualDesc::Read(const scene::Node &node) {
	VisualDesc desc;
	const scene::Component *component = node.FindComponent(String(component::VISUAL));
	if (!component)
		return desc;
	const scene::PropertyMap &props = component->props;
	auto value = [&props](const char *key) { return props.Find(String(key)); };
	if (const scene::PropertyValue *shapeValue = value("shape"))
		desc.shape = ShapeKindFromName(shapeValue->AsString("box")).UnwrapOr(ShapeKind::BOX);
	if (const scene::PropertyValue *dims = value("dimensions"))
		desc.dimensions = dims->AsVec3(desc.dimensions);
	if (const scene::PropertyValue *segments = value("segments"))
		desc.segments = int(segments->AsInt(24));
	if (const scene::PropertyValue *source = value("source"))
		desc.source = source->AsString();
	if (const scene::PropertyValue *kind = value("material"))
		desc.material.kind = MaterialKindFromName(kind->AsString("plastic")).UnwrapOr(MaterialKind::PLASTIC);
	if (const scene::PropertyValue *color = value("base_color"))
		desc.material.baseColor = color->AsColor(desc.material.baseColor);
	if (const scene::PropertyValue *metallic = value("metallic"))
		desc.material.metallic = metallic->AsFloat(0.f);
	if (const scene::PropertyValue *roughness = value("roughness"))
		desc.material.roughness = roughness->AsFloat(0.5f);
	if (const scene::PropertyValue *doubleSided = value("double_sided"))
		desc.material.doubleSided = doubleSided->AsBool(false);
	if (const scene::PropertyValue *wireframe = value("wireframe"))
		desc.material.wireframe = wireframe->AsBool(false);
	return desc;
}

void VisualDesc::Write(scene::Node &node) const {
	scene::Component component;
	component.type = String(component::VISUAL);
	component.props.Set(String("shape"), scene::PropertyValue::Str(String(ShapeKindName(shape))));
	component.props.Set(String("dimensions"), scene::PropertyValue::Vec3(dimensions));
	component.props.Set(String("segments"), scene::PropertyValue::Int(segments));
	if (!source.IsEmpty())
		component.props.Set(String("source"), scene::PropertyValue::Resource(source));
	component.props.Set(String("material"), scene::PropertyValue::Str(String(MaterialKindName(material.kind))));
	component.props.Set(String("base_color"), scene::PropertyValue::Color(material.baseColor));
	component.props.Set(String("metallic"), scene::PropertyValue::Float(double(material.metallic)));
	component.props.Set(String("roughness"), scene::PropertyValue::Float(double(material.roughness)));
	component.props.Set(String("double_sided"), scene::PropertyValue::Bool(material.doubleSided));
	component.props.Set(String("wireframe"), scene::PropertyValue::Bool(material.wireframe));
	node.SetComponent(std::move(component));
}

const char * CanvasItemKindName(CanvasItemKind kind) noexcept {
	switch (kind) {
		case CanvasItemKind::RECT:
			return "rect";
		case CanvasItemKind::CIRCLE:
			return "circle";
		case CanvasItemKind::POLYGON:
			return "polygon";
		case CanvasItemKind::LINE:
			return "line";
		case CanvasItemKind::SPRITE:
			return "sprite";
		case CanvasItemKind::TEXT:
			return "text";
	}
	return "rect";
}

const char * CanvasItemKindLabel(CanvasItemKind kind) noexcept {
	switch (kind) {
		case CanvasItemKind::RECT:
			return "Rectangle";
		case CanvasItemKind::CIRCLE:
			return "Cercle / ellipse";
		case CanvasItemKind::POLYGON:
			return "Polygone";
		case CanvasItemKind::LINE:
			return "Trait";
		case CanvasItemKind::SPRITE:
			return "Image";
		case CanvasItemKind::TEXT:
			return "Texte";
	}
	return "Rectangle";
}

Option<CanvasItemKind> CanvasItemKindFromName(const String &name) {
	for (CanvasItemKind kind : CANVAS_ITEM_KINDS)
		if (name == CanvasItemKindName(kind))
			return Some(kind);
	return NONE;
}

const char * TextAlign2DName(TextAlign2D align) noexcept {
	return align == TextAlign2D::LEFT ? "left" : align == TextAlign2D::RIGHT ? "right" : "center";
}

TextAlign2D TextAlign2DFromName(const String &name) noexcept {
	return name == "left" ? TextAlign2D::LEFT : name == "right" ? TextAlign2D::RIGHT : TextAlign2D::CENTER;
}

// ── CanvasItemDesc ───────────────────────────────────────────────────────────

bool CanvasItemDesc::Has(const scene::Node &node) noexcept {
	return node.HasComponent(String(component::CANVAS_ITEM));
}

CanvasItemDesc CanvasItemDesc::Read(const scene::Node &node) {
	CanvasItemDesc desc;
	const scene::Component *component = node.FindComponent(String(component::CANVAS_ITEM));
	if (!component)
		return desc;
	const scene::PropertyMap &props = component->props;
	auto value = [&props](const char *key) { return props.Find(String(key)); };
	if (const scene::PropertyValue *v = value("kind"))
		desc.kind = CanvasItemKindFromName(v->AsString("rect")).UnwrapOr(CanvasItemKind::RECT);
	if (const scene::PropertyValue *v = value("size"))
		desc.size = v->AsVec2(desc.size);
	if (const scene::PropertyValue *v = value("color"))
		desc.color = v->AsColor(desc.color);
	if (const scene::PropertyValue *v = value("filled"))
		desc.filled = v->AsBool(true);
	if (const scene::PropertyValue *v = value("outline"))
		desc.outline = v->AsFloat(0.f);
	if (const scene::PropertyValue *v = value("outline_color"))
		desc.outlineColor = v->AsColor(desc.outlineColor);
	if (const scene::PropertyValue *v = value("centered"))
		desc.centered = v->AsBool(true);
	if (const scene::PropertyValue *v = value("points"))
		for (const scene::PropertyValue &point : v->AsArray())
			desc.points.push_back(point.AsVec2());
	if (const scene::PropertyValue *v = value("texture"))
		desc.texture = v->AsString();
	if (const scene::PropertyValue *v = value("flip_h"))
		desc.flipH = v->AsBool(false);
	if (const scene::PropertyValue *v = value("flip_v"))
		desc.flipV = v->AsBool(false);
	if (const scene::PropertyValue *v = value("text"))
		desc.text = v->AsString();
	if (const scene::PropertyValue *v = value("font_size"))
		desc.fontSize = v->AsFloat(24.f);
	if (const scene::PropertyValue *v = value("align"))
		desc.align = TextAlign2DFromName(v->AsString("center"));
	if (const scene::PropertyValue *v = value("z"))
		desc.z = int(v->AsInt(0));
	return desc;
}

void CanvasItemDesc::Write(scene::Node &node) const {
	scene::Component component;
	component.type = String(component::CANVAS_ITEM);
	scene::PropertyMap &props = component.props;
	props.Set(String("kind"), scene::PropertyValue::Str(String(CanvasItemKindName(kind))));
	props.Set(String("color"), scene::PropertyValue::Color(color));
	props.Set(String("z"), scene::PropertyValue::Int(z));
	props.Set(String("centered"), scene::PropertyValue::Bool(centered));
	switch (kind) {
		case CanvasItemKind::TEXT:
			props.Set(String("text"), scene::PropertyValue::Str(text));
			props.Set(String("font_size"), scene::PropertyValue::Float(double(fontSize)));
			props.Set(String("align"), scene::PropertyValue::Str(String(TextAlign2DName(align))));
			break;
		case CanvasItemKind::SPRITE:
			props.Set(String("size"), scene::PropertyValue::Vec2(size));
			props.Set(String("texture"), scene::PropertyValue::Resource(texture));
			props.Set(String("flip_h"), scene::PropertyValue::Bool(flipH));
			props.Set(String("flip_v"), scene::PropertyValue::Bool(flipV));
			break;
		case CanvasItemKind::POLYGON:
		case CanvasItemKind::LINE: {
			std::vector<scene::PropertyValue> list;
			for (const math::FVector2 &point : points)
				list.push_back(scene::PropertyValue::Vec2(point));
			props.Set(String("points"), scene::PropertyValue::Array(std::move(list)));
			break;
		}
		default:
			props.Set(String("size"), scene::PropertyValue::Vec2(size));
			break;
	}
	if (kind != CanvasItemKind::SPRITE && kind != CanvasItemKind::TEXT) {
		props.Set(String("filled"), scene::PropertyValue::Bool(filled));
		props.Set(String("outline"), scene::PropertyValue::Float(double(outline)));
		props.Set(String("outline_color"), scene::PropertyValue::Color(outlineColor));
	}
	node.SetComponent(std::move(component));
}

// ── Camera2DDesc ─────────────────────────────────────────────────────────────

bool Camera2DDesc::Has(const scene::Node &node) noexcept {
	return node.HasComponent(String(component::CAMERA_2D));
}

Camera2DDesc Camera2DDesc::Read(const scene::Node &node) {
	Camera2DDesc desc;
	if (const scene::PropertyValue *v = ComponentProp(node, component::CAMERA_2D, "zoom"))
		desc.zoom = v->AsFloat(1.f);
	if (const scene::PropertyValue *v = ComponentProp(node, component::CAMERA_2D, "current"))
		desc.current = v->AsBool(false);
	return desc;
}

void Camera2DDesc::Write(scene::Node &node) const {
	scene::Component component;
	component.type = String(component::CAMERA_2D);
	component.props.Set(String("zoom"), scene::PropertyValue::Float(double(zoom)));
	component.props.Set(String("current"), scene::PropertyValue::Bool(current));
	node.SetComponent(std::move(component));
}

bool Is2DNode(const scene::Node &node) {
	const String &type = node.type;
	return type == node_kind::NODE2D || type == node_kind::SHAPE2D || type == node_kind::SPRITE2D ||
		   type == node_kind::LABEL2D || type == node_kind::CAMERA2D || type == node_kind::CANVAS_LAYER ||
		   CanvasItemDesc::Has(node) || Camera2DDesc::Has(node);
}

const char * LightKindName(LightKind kind) noexcept {
	return kind == LightKind::SPOT ? "spot" : "point";
}

Option<LightKind> LightKindFromName(const String &name) {
	String key = name.ToLower();
	if (key == "point")
		return Some(LightKind::POINT);
	if (key == "spot")
		return Some(LightKind::SPOT);
	return NONE;
}

// ── LightDesc ────────────────────────────────────────────────────────────────

bool LightDesc::Has(const scene::Node &node) noexcept {
	return node.HasComponent(String(component::LIGHT));
}

LightDesc LightDesc::Read(const scene::Node &node) {
	LightDesc desc;
	auto prop = [&node](const char *key) { return ComponentProp(node, component::LIGHT, key); };
	if (const auto *v = prop("kind"))
		desc.kind = LightKindFromName(v->AsString("point")).UnwrapOr(LightKind::POINT);
	if (const auto *v = prop("color"))
		desc.color = v->AsColor(desc.color);
	if (const auto *v = prop("intensity"))
		desc.intensity = v->AsFloat(desc.intensity);
	if (const auto *v = prop("range"))
		desc.range = v->AsFloat(desc.range);
	if (const auto *v = prop("spot_angle"))
		desc.spotAngle = v->AsFloat(desc.spotAngle);
	if (const auto *v = prop("penumbra"))
		desc.penumbra = v->AsFloat(desc.penumbra);
	if (const auto *v = prop("cast_shadow"))
		desc.castShadow = v->AsBool(desc.castShadow);
	return desc;
}

void LightDesc::Write(scene::Node &node) const {
	scene::Component component;
	component.type = String(component::LIGHT);
	component.props.Set(String("kind"), scene::PropertyValue::Str(String(LightKindName(kind))));
	component.props.Set(String("color"), scene::PropertyValue::Color(color));
	component.props.Set(String("intensity"), scene::PropertyValue::Float(double(intensity)));
	component.props.Set(String("range"), scene::PropertyValue::Float(double(range)));
	component.props.Set(String("spot_angle"), scene::PropertyValue::Float(double(spotAngle)));
	component.props.Set(String("penumbra"), scene::PropertyValue::Float(double(penumbra)));
	component.props.Set(String("cast_shadow"), scene::PropertyValue::Bool(castShadow));
	node.SetComponent(std::move(component));
}

// ── CameraNodeDesc ───────────────────────────────────────────────────────────

bool CameraNodeDesc::Has(const scene::Node &node) noexcept {
	return node.HasComponent(String(component::CAMERA));
}

CameraNodeDesc CameraNodeDesc::Read(const scene::Node &node) {
	CameraNodeDesc desc;
	if (const auto *v = ComponentProp(node, component::CAMERA, "fov"))
		desc.fovDegrees = v->AsFloat(desc.fovDegrees);
	if (const auto *v = ComponentProp(node, component::CAMERA, "current"))
		desc.current = v->AsBool(desc.current);
	return desc;
}

void CameraNodeDesc::Write(scene::Node &node) const {
	scene::Component component;
	component.type = String(component::CAMERA);
	component.props.Set(String("fov"), scene::PropertyValue::Float(double(fovDegrees)));
	component.props.Set(String("current"), scene::PropertyValue::Bool(current));
	node.SetComponent(std::move(component));
}

// ── TriggerDesc ──────────────────────────────────────────────────────────────

bool TriggerDesc::Has(const scene::Node &node) noexcept {
	return node.HasComponent(String(component::TRIGGER));
}

TriggerDesc TriggerDesc::Read(const scene::Node &node) {
	TriggerDesc desc;
	if (const auto *v = ComponentProp(node, component::TRIGGER, "half_extents"))
		desc.halfExtents = v->AsVec3(desc.halfExtents);
	if (const auto *v = ComponentProp(node, component::TRIGGER, "event"))
		desc.event = v->AsString();
	if (const auto *v = ComponentProp(node, component::TRIGGER, "once"))
		desc.once = v->AsBool(false);
	return desc;
}

void TriggerDesc::Write(scene::Node &node) const {
	scene::Component component;
	component.type = String(component::TRIGGER);
	component.props.Set(String("half_extents"), scene::PropertyValue::Vec3(halfExtents));
	component.props.Set(String("event"), scene::PropertyValue::Str(event));
	component.props.Set(String("once"), scene::PropertyValue::Bool(once));
	node.SetComponent(std::move(component));
}

// ── ScriptRef ────────────────────────────────────────────────────────────────

bool ScriptRef::Has(const scene::Node &node) noexcept {
	return node.HasComponent(String(component::SCRIPT));
}

ScriptRef ScriptRef::Read(const scene::Node &node) {
	ScriptRef ref;
	if (const auto *v = ComponentProp(node, component::SCRIPT, "script"))
		ref.script = v->AsString();
	return ref;
}

void ScriptRef::Write(scene::Node &node) const {
	scene::Component component;
	component.type = String(component::SCRIPT);
	component.props.Set(String("script"), scene::PropertyValue::Str(script));
	node.SetComponent(std::move(component));
}

// ── ScriptAsset ──────────────────────────────────────────────────────────────

data::NodePtr ScriptAsset::ToJson() const {
	auto node = data::Node::MakeObject();
	node->Set("name", data::Node::MakeString(name));
	node->Set("description", data::Node::MakeString(description));
	node->Set("source", data::Node::MakeString(source));
	return node;
}

Result<ScriptAsset, String> ScriptAsset::FromJson(const data::NodePtr &node) {
	if (!node || !node->IsObject())
		return Err(String("script : objet JSON attendu"));
	ScriptAsset asset;
	asset.name = json::Str(node->Get("name"));
	if (asset.name.IsEmpty())
		return Err(String("script : champ `name` manquant ou vide"));
	asset.description = json::Str(node->Get("description"));
	asset.source = json::Str(node->Get("source"));
	return Ok(std::move(asset));
}

// ── ObjectDesc ───────────────────────────────────────────────────────────────

ObjectDesc ObjectDesc::Folder(String folderName) {
	ObjectDesc desc = Group(std::move(folderName));
	desc.type = String(node_kind::FOLDER);
	return desc;
}

ObjectDesc ObjectDesc::Light(String lightName, LightDesc lightDesc) {
	ObjectDesc desc = Group(std::move(lightName));
	desc.type = String(node_kind::LIGHT);
	desc.light = Some(lightDesc);
	return desc;
}

ObjectDesc ObjectDesc::Group(String groupName) {
	ObjectDesc desc;
	desc.name = std::move(groupName);
	desc.type = String(node_kind::GROUP);
	desc.hasVisual = false;
	return desc;
}

scene::Node ObjectDesc::ToNode() const {
	scene::Node node;
	node.name = name;
	node.transform = transform;
	node.visible = visible;
	node.type = !type.IsEmpty()  ? type
				: hasVisual        ? String(node_kind::MESH)
				: light.IsSome()   ? String(node_kind::LIGHT)
				: camera.IsSome()  ? String(node_kind::CAMERA)
				: trigger.IsSome() ? String(node_kind::TRIGGER)
								   : String(node_kind::GROUP);
	if (light.IsSome())
		light.Value().Write(node);
	if (camera.IsSome())
		camera.Value().Write(node);
	if (trigger.IsSome())
		trigger.Value().Write(node);
	if (!script.IsEmpty())
		ScriptRef{script}.Write(node);
	for (const scene::Component &extra : components)
		node.SetComponent(extra);
	if (hasVisual) {
		VisualDesc visual;
		visual.shape = shape;
		visual.dimensions = dimensions;
		visual.segments = segments;
		visual.source = source;
		visual.material = material;
		visual.Write(node);
	}
	physics.Write(node);
	SetTag(node, tag);
	return node;
}

ObjectDesc ObjectDesc::FromNode(const scene::Node &node) {
	ObjectDesc desc;
	desc.name = node.name;
	desc.type = node.type;
	desc.transform = node.transform;
	desc.visible = node.visible;
	desc.tag = TagOf(node);
	desc.hasVisual = VisualDesc::Has(node);
	VisualDesc visual = VisualDesc::Read(node);
	desc.shape = visual.shape;
	desc.dimensions = visual.dimensions;
	desc.segments = visual.segments;
	desc.source = visual.source;
	desc.material = visual.material;
	desc.physics = PhysicsDesc::Read(node);
	if (LightDesc::Has(node))
		desc.light = Some(LightDesc::Read(node));
	if (CameraNodeDesc::Has(node))
		desc.camera = Some(CameraNodeDesc::Read(node));
	if (TriggerDesc::Has(node))
		desc.trigger = Some(TriggerDesc::Read(node));
	desc.script = ScriptRef::Read(node).script;
	for (const scene::Component &component : node.components)
		if (!IsBuiltinComponent(component.type))
			desc.components.push_back(component);
	return desc;
}

Result<ObjectDesc, String> ObjectDesc::FromLegacyJson(const data::NodePtr &node) {
	if (!node || !node->IsObject())
		return Err(String("objet : objet JSON attendu"));
	ObjectDesc desc;
	desc.name = json::Str(node->Get("name"));
	if (desc.name.IsEmpty())
		return Err(String("objet : champ `name` manquant ou vide"));
	desc.parent = json::Str(node->Get("parent"));
	desc.tag = json::Str(node->Get("tag"));
	desc.source = json::Str(node->Get("source"));
	desc.shape = ShapeKindFromName(json::Str(node->Get("shape"), "box")).UnwrapOr(ShapeKind::BOX);
	desc.dimensions = json::ToVec3(node->Get("dimensions"), math::FVector3{1.f, 1.f, 1.f});
	desc.segments = json::Int(node->Get("segments"), 24);
	desc.visible = json::Bool(node->Get("visible"), true);
	if (auto transform = node->Get("transform"); transform && transform->IsObject()) {
		desc.transform.position = json::ToVec3(transform->Get("position"));
		desc.transform.SetEulerDegrees(json::ToVec3(transform->Get("rotation")));
		desc.transform.scale = json::ToVec3(transform->Get("scale"), math::FVector3{1.f, 1.f, 1.f});
	}
	desc.material = MaterialDesc::FromJson(node->Get("material"));
	desc.physics = PhysicsDesc::FromJson(node->Get("physics"));
	return Ok(std::move(desc));
}

Option<ObjectDesc> MakeNode2DFromTemplate(const String &key, const String &label) {
	ObjectDesc desc = ObjectDesc::Group(label);
	CanvasItemDesc item;
	item.color = sdl3::Color{92, 156, 236, 255};
	if (key == "rect2d") {
		desc.type = String(node_kind::SHAPE2D);
		item.size = {128.f, 80.f};
	} else if (key == "circle2d") {
		desc.type = String(node_kind::SHAPE2D);
		item.kind = CanvasItemKind::CIRCLE;
		item.size = {96.f, 96.f};
		item.color = sdl3::Color{236, 176, 72, 255};
	} else if (key == "polygon2d") {
		desc.type = String(node_kind::SHAPE2D);
		item.kind = CanvasItemKind::POLYGON;
		item.points = {{0.f, -56.f}, {52.f, 38.f}, {-52.f, 38.f}};
		item.color = sdl3::Color{120, 206, 128, 255};
	} else if (key == "line2d") {
		desc.type = String(node_kind::SHAPE2D);
		item.kind = CanvasItemKind::LINE;
		item.points = {{-60.f, 0.f}, {0.f, -30.f}, {60.f, 0.f}};
		item.outline = 4.f;
		item.color = sdl3::Color{232, 232, 240, 255};
	} else if (key == "sprite2d") {
		desc.type = String(node_kind::SPRITE2D);
		item.kind = CanvasItemKind::SPRITE;
		item.size = {96.f, 96.f};
		item.color = sdl3::Color{255, 255, 255, 255};
	} else if (key == "label2d") {
		desc.type = String(node_kind::LABEL2D);
		item.kind = CanvasItemKind::TEXT;
		item.text = String("Texte");
		item.fontSize = 32.f;
		item.color = sdl3::Color{240, 240, 245, 255};
	} else if (key == "camera2d") {
		desc.type = String(node_kind::CAMERA2D);
		scene::Node scratch;
		Camera2DDesc{}.Write(scratch);
		desc.components.push_back(scratch.components.front());
		return Some(std::move(desc));
	} else if (key == "node2d" || key == "canvas_layer") {
		desc.type = String(key == "node2d" ? node_kind::NODE2D : node_kind::CANVAS_LAYER);
		return Some(std::move(desc));
	} else {
		return NONE;
	}
	scene::Node scratch;
	item.Write(scratch);
	desc.components.push_back(scratch.components.front());
	return Some(std::move(desc));
}

Option<ObjectDesc> MakeNodeFromTemplate(const String &key) {
	const char *label = nullptr;
	for (const NodeTemplate &entry : NODE_TEMPLATES)
		if (key == entry.key)
			label = entry.label;
	if (!label)
		return NONE;
	if (Option<ObjectDesc> flat = MakeNode2DFromTemplate(key, String(label)); flat.IsSome())
		return flat;

	if (Option<ShapeKind> shape = ShapeKindFromName(key); shape.IsSome()) {
		ObjectDesc desc;
		desc.name = String(label);
		desc.shape = shape.Unwrap();
		desc.dimensions = shape.Unwrap() == ShapeKind::PLANE   ? math::FVector3{4.f, 1.f, 4.f}
						  : shape.Unwrap() == ShapeKind::TORUS ? math::FVector3{2.f, 0.6f, 2.f}
															   : math::FVector3{1.f, 1.f, 1.f};
		desc.material.baseColor = sdl3::Color{168, 176, 196, 255};
		desc.physics.halfExtents = desc.dimensions * 0.5f;
		return Some(std::move(desc));
	}
	if (key == "point_light" || key == "spot_light") {
		LightDesc light;
		light.kind = key == "spot_light" ? LightKind::SPOT : LightKind::POINT;
		light.color = sdl3::Color{255, 238, 210, 255};
		return Some(ObjectDesc::Light(String(label), light));
	}
	if (key == "folder")
		return Some(ObjectDesc::Folder(String(label)));
	ObjectDesc desc = ObjectDesc::Group(String(label));
	if (key == "trigger") {
		desc.type = String(node_kind::TRIGGER);
		desc.trigger = Some(TriggerDesc{});
	} else if (key == "camera") {
		desc.type = String(node_kind::CAMERA);
		desc.camera = Some(CameraNodeDesc{});
	} else if (key == "spawn") {
		desc.type = String(node_kind::SPAWN);
	} else {
		desc.type = String(scene::node_type::NODE3D);
	}
	return Some(std::move(desc));
}

// ── EnvironmentDesc ──────────────────────────────────────────────────────────

data::NodePtr EnvironmentDesc::ToJson() const {
	auto node = data::Node::MakeObject();
	node->Set("sun_direction", json::FromVec3(sunDirection));
	node->Set("sun_intensity", data::Node::MakeFloat(double(sunIntensity)));
	node->Set("sun_color", json::FromColor(sunColor));
	node->Set("ambient_color", json::FromColor(ambientColor));
	node->Set("background_color", json::FromColor(backgroundColor));
	node->Set("gravity", json::FromVec3(gravity));
	return node;
}

EnvironmentDesc EnvironmentDesc::FromJson(const data::NodePtr &node) {
	EnvironmentDesc desc;
	if (!node || !node->IsObject())
		return desc;
	desc.sunDirection = json::ToVec3(node->Get("sun_direction"), desc.sunDirection);
	desc.sunIntensity = json::Float(node->Get("sun_intensity"), desc.sunIntensity);
	desc.sunColor = json::ToColor(node->Get("sun_color"), desc.sunColor);
	desc.ambientColor = json::ToColor(node->Get("ambient_color"), desc.ambientColor);
	desc.backgroundColor = json::ToColor(node->Get("background_color"), desc.backgroundColor);
	desc.gravity = json::ToVec3(node->Get("gravity"), desc.gravity);
	return desc;
}

// ── CameraDesc ───────────────────────────────────────────────────────────────

data::NodePtr CameraDesc::ToJson() const {
	auto node = data::Node::MakeObject();
	node->Set("edit_position", json::FromVec3(editPosition));
	node->Set("edit_yaw", data::Node::MakeFloat(double(editYaw)));
	node->Set("edit_pitch", data::Node::MakeFloat(double(editPitch)));
	node->Set("play_position", json::FromVec3(playPosition));
	node->Set("play_yaw", data::Node::MakeFloat(double(playYaw)));
	node->Set("play_pitch", data::Node::MakeFloat(double(playPitch)));
	return node;
}

CameraDesc CameraDesc::FromJson(const data::NodePtr &node) {
	CameraDesc desc;
	if (!node || !node->IsObject())
		return desc;
	desc.editPosition = json::ToVec3(node->Get("edit_position"), desc.editPosition);
	desc.editYaw = json::Float(node->Get("edit_yaw"), desc.editYaw);
	desc.editPitch = json::Float(node->Get("edit_pitch"), desc.editPitch);
	desc.playPosition = json::ToVec3(node->Get("play_position"), desc.playPosition);
	desc.playYaw = json::Float(node->Get("play_yaw"), desc.playYaw);
	desc.playPitch = json::Float(node->Get("play_pitch"), desc.playPitch);
	return desc;
}

// ── Canvas2DDesc ─────────────────────────────────────────────────────────────

math::FVector2 Canvas2DDesc::Size() const noexcept {
	return {float(width > 0 ? width : 1), float(height > 0 ? height : 1)};
}

data::NodePtr Canvas2DDesc::ToJson() const {
	auto node = data::Node::MakeObject();
	node->Set("width", data::Node::MakeInt(width));
	node->Set("height", data::Node::MakeInt(height));
	node->Set("render_3d", data::Node::MakeBool(render3d));
	node->Set("background", json::FromColor(background));
	return node;
}

Canvas2DDesc Canvas2DDesc::FromJson(const data::NodePtr &node) {
	Canvas2DDesc desc;
	if (!node || !node->IsObject())
		return desc;
	desc.width = sdl3::Clamp(json::Int(node->Get("width"), desc.width), 16, 16384);
	desc.height = sdl3::Clamp(json::Int(node->Get("height"), desc.height), 16, 16384);
	desc.render3d = json::Bool(node->Get("render_3d"), desc.render3d);
	desc.background = json::ToColor(node->Get("background"), desc.background);
	return desc;
}

// ── SceneDesc ────────────────────────────────────────────────────────────────

void SceneDesc::SetName(String sceneName) {
	name = sceneName;
	(void)tree.Rename(tree.Root(), std::move(sceneName));
}

scene::NodeId SceneDesc::FindId(const String &objectName) const {
	return tree.FindByName(objectName);
}

scene::Node * SceneDesc::Find(const String &objectName) noexcept {
	scene::NodeId id = FindId(objectName);
	return id.Valid() ? tree.Get(id) : nullptr;
}

const scene::Node * SceneDesc::Find(const String &objectName) const noexcept {
	scene::NodeId id = FindId(objectName);
	return id.Valid() ? tree.Get(id) : nullptr;
}

scene::NodeId SceneDesc::Resolve(const String &pathOrName) const {
	if (pathOrName.IsEmpty())
		return tree.Root();
	if (pathOrName.Contains('/')) {
		if (scene::NodeId id = tree.Resolve(pathOrName); id.Valid())
			return id;
	}
	return FindId(pathOrName);
}

std::vector<scene::NodeId> SceneDesc::Objects() const {
	std::vector<scene::NodeId> out;
	tree.Traverse(tree.Root(), [&](scene::NodeId id, const scene::Node &) {
		if (id != tree.Root())
			out.push_back(id);
	});
	return out;
}

String SceneDesc::UniqueName(const String &base) const {
	String wanted = base.IsEmpty() ? String("Object") : base;
	if (!FindId(wanted).Valid())
		return wanted;
	for (int suffix = 2; suffix < 100000; ++suffix) {
		String candidate = String::Format("%s %d", wanted.CStr(), suffix);
		if (!FindId(candidate).Valid())
			return candidate;
	}
	return wanted;
}

scene::NodeId SceneDesc::AddNode(ObjectDesc object) {
	scene::NodeId parent = object.parent.IsEmpty() ? tree.Root() : Resolve(object.parent);
	if (!parent.Valid())
		parent = tree.Root();
	object.name = UniqueName(object.name.IsEmpty() ? String("Object") : object.name);
	return tree.Add(parent, object.ToNode());
}

String SceneDesc::Add(ObjectDesc object) {
	scene::NodeId id = AddNode(std::move(object));
	const scene::Node *node = tree.Get(id);
	return node ? node->name : String();
}

bool SceneDesc::Remove(const String &objectName) {
	scene::NodeId id = FindId(objectName);
	return id.Valid() && tree.Remove(id);
}

std::vector<scene::NodeId> SceneDesc::WithTag(const String &wantedTag) const {
	std::vector<scene::NodeId> found;
	tree.Traverse(tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
		if (id != tree.Root() && TagOf(node) == wantedTag)
			found.push_back(id);
	});
	return found;
}

data::NodePtr SceneDesc::ToJson() const {
	auto node = data::Node::MakeObject();
	node->Set("name", data::Node::MakeString(name));
	node->Set("description", data::Node::MakeString(description));
	node->Set("gameplay_script", data::Node::MakeString(gameplayScript));
	node->Set("environment", environment.ToJson());
	node->Set("camera", camera.ToJson());
	node->Set("canvas2d", canvas.ToJson());
	node->Set("tree", tree.ToJson());
	return node;
}

Result<SceneDesc, String> SceneDesc::FromJson(const data::NodePtr &node) {
	if (!node || !node->IsObject())
		return Err(String("scène : objet JSON attendu"));
	SceneDesc scene;
	scene.SetName(json::Str(node->Get("name")));
	if (scene.name.IsEmpty())
		return Err(String("scène : champ `name` manquant ou vide"));
	scene.description = json::Str(node->Get("description"));
	scene.gameplayScript = json::Str(node->Get("gameplay_script"));
	scene.environment = EnvironmentDesc::FromJson(node->Get("environment"));
	scene.camera = CameraDesc::FromJson(node->Get("camera"));
	scene.canvas = Canvas2DDesc::FromJson(node->Get("canvas2d"));

	if (auto treeJson = node->Get("tree"); treeJson && treeJson->IsObject()) {
		auto tree = ::scene::NodeTree::FromJson(treeJson);
		if (tree.IsError())
			return Err(String::Format("scène « %s » : %s", scene.name.CStr(), tree.Error().CStr()));
		scene.tree = std::move(tree).Unwrap();
		scene.SetName(scene.name); // la racine suit toujours le nom de la scène
		return Ok(std::move(scene));
	}
	// Format 2 : liste plate + champ `parent` portant un nom.
	if (auto array = node->Get("objects"); array && array->IsArray()) {
		auto imported = FromLegacyObjects(scene, array);
		if (imported.IsSome())
			return Err(imported.Unwrap());
	}
	return Ok(std::move(scene));
}

Option<String> SceneDesc::FromLegacyObjects(SceneDesc &scene, const data::NodePtr &array) {
	std::vector<ObjectDesc> objects;
	for (size_t i = 0; i < array->GetSize(); ++i) {
		auto object = ObjectDesc::FromLegacyJson(array->At(i));
		if (object.IsError())
			return Some(String::Format("scène « %s » : %s", scene.name.CStr(), object.Error().CStr()));
		objects.push_back(std::move(object).Unwrap());
	}
	std::vector<scene::NodeId> created;
	created.reserve(objects.size());
	for (const ObjectDesc &object : objects)
		created.push_back(scene.tree.Add(scene.tree.Root(), object.ToNode()));
	for (size_t i = 0; i < objects.size(); ++i) {
		if (objects[i].parent.IsEmpty())
			continue;
		scene::NodeId parent = scene.FindId(objects[i].parent);
		if (parent.Valid() && parent != created[i])
			(void)scene.tree.Reparent(created[i], parent, scene::ReparentMode::KEEP_LOCAL);
	}
	return NONE;
}

// ── Project ──────────────────────────────────────────────────────────────────

ScriptAsset * Project::FindScript(const String &scriptName) noexcept {
	for (ScriptAsset &script : scripts)
		if (script.name == scriptName)
			return &script;
	return nullptr;
}

const ScriptAsset * Project::FindScript(const String &scriptName) const noexcept {
	for (const ScriptAsset &script : scripts)
		if (script.name == scriptName)
			return &script;
	return nullptr;
}

SceneDesc * Project::FindScene(const String &sceneName) noexcept {
	for (SceneDesc &scene : scenes)
		if (scene.name == sceneName)
			return &scene;
	return nullptr;
}

const SceneDesc * Project::FindScene(const String &sceneName) const noexcept {
	for (const SceneDesc &scene : scenes)
		if (scene.name == sceneName)
			return &scene;
	return nullptr;
}

SceneDesc * Project::ActiveScene() noexcept {
	if (SceneDesc *scene = FindScene(activeScene))
		return scene;
	return scenes.empty() ? nullptr : &scenes.front();
}

const SceneDesc * Project::ActiveScene() const noexcept {
	if (const SceneDesc *scene = FindScene(activeScene))
		return scene;
	return scenes.empty() ? nullptr : &scenes.front();
}

bool Project::SetActiveScene(const String &sceneName) {
	if (!FindScene(sceneName))
		return false;
	activeScene = sceneName;
	return true;
}

std::vector<String> Project::SceneNames() const {
	std::vector<String> names;
	names.reserve(scenes.size());
	for (const SceneDesc &scene : scenes)
		names.push_back(scene.name);
	return names;
}

size_t Project::TotalObjectCount() const noexcept {
	size_t total = 0;
	for (const SceneDesc &scene : scenes)
		total += scene.ObjectCount();
	return total;
}

data::NodePtr Project::ToJson() const {
	auto root = data::Node::MakeObject();
	root->Set("format", data::Node::MakeString("game_editor.project"));
	root->Set("version", data::Node::MakeInt(FORMAT_VERSION));
	root->Set("name", data::Node::MakeString(name));
	root->Set("active_scene", data::Node::MakeString(activeScene));
	auto array = data::Node::MakeArray();
	for (const SceneDesc &scene : scenes)
		array->Push(scene.ToJson());
	root->Set("scenes", array);
	auto scriptArray = data::Node::MakeArray();
	for (const ScriptAsset &script : scripts)
		scriptArray->Push(script.ToJson());
	root->Set("scripts", scriptArray);
	return root;
}

Result<Project, String> Project::FromJson(const data::NodePtr &root) {
	if (!root || !root->IsObject())
		return Err(String("projet : racine JSON invalide (objet attendu)"));
	auto scenesNode = root->Get("scenes");
	if (!scenesNode || !scenesNode->IsArray())
		return Err(String("projet : tableau `scenes` manquant"));

	int version = json::Int(root->Get("version"), FORMAT_VERSION);
	if (version > FORMAT_VERSION)
		return Err(String::Format("projet : version de format %d trop récente (max supportée : %d)", version,
								  FORMAT_VERSION));

	Project project;
	project.name = json::Str(root->Get("name"), "Projet sans titre");
	project.activeScene = json::Str(root->Get("active_scene"));
	for (size_t i = 0; i < scenesNode->GetSize(); ++i) {
		auto scene = SceneDesc::FromJson(scenesNode->At(i));
		if (scene.IsError())
			return Err(scene.Error());
		project.scenes.push_back(std::move(scene).Unwrap());
	}
	if (project.scenes.empty())
		return Err(String("projet : aucune scène"));
	// Bibliothèque de scripts : facultative (absente des fichiers d'avant
	// la version 4).
	if (auto scriptsNode = root->Get("scripts"); scriptsNode && scriptsNode->IsArray()) {
		for (size_t i = 0; i < scriptsNode->GetSize(); ++i) {
			auto script = ScriptAsset::FromJson(scriptsNode->At(i));
			if (script.IsError())
				return Err(script.Error());
			project.scripts.push_back(std::move(script).Unwrap());
		}
	}
	if (!project.FindScene(project.activeScene))
		project.activeScene = project.scenes.front().name;
	return Ok(std::move(project));
}

String Project::EncodeJson() const {
	data::JsonDocument document;
	document.SetRoot(ToJson());
	return document.EncodeStr();
}

Result<Project, String> Project::DecodeJson(const String &text) {
	data::JsonDocument document;
	auto error = document.DecodeStr(text);
	if (error.IsSome())
		return Err(error.Unwrap().Format());
	return FromJson(document.GetRoot());
}

} // namespace game_editor
