// Définitions de scene/node.hpp
#include "scene/node.hpp"

namespace scene {

// ── Transform ────────────────────────────────────────────────────────────────

math::FMatrix4 Transform::Matrix() const noexcept {
	return math::ComposeTRS(position, rotation, scale);
}

Transform Transform::FromMatrix(const math::FMatrix4& m) noexcept {
	math::TRS trs = math::DecomposeTRS(m);
	return Transform{trs.translation, trs.rotation, trs.scale};
}

math::FVector3 Transform::EulerDegrees() const noexcept {
	math::FVector3 e = rotation.ToEuler();
	return {e.x * RAD2DEG, e.y * RAD2DEG, e.z * RAD2DEG};
}

void Transform::SetEulerDegrees(const math::FVector3& degrees) noexcept {
	rotation = math::FQuaternion::FromEuler(degrees.x * DEG2RAD, degrees.y * DEG2RAD,
											degrees.z * DEG2RAD);
}

data::NodePtr Transform::ToJson() const {
	auto vec3 = [](const math::FVector3& v) {
		auto array = data::Node::MakeArray();
		array->Push(data::Node::MakeFloat(double(v.x)));
		array->Push(data::Node::MakeFloat(double(v.y)));
		array->Push(data::Node::MakeFloat(double(v.z)));
		return array;
	};
	auto node = data::Node::MakeObject();
	node->Set("position", vec3(position));
	node->Set("rotation", vec3(EulerDegrees()));
	node->Set("scale", vec3(scale));
	return node;
}

Transform Transform::FromJson(const data::NodePtr& node) {
	Transform transform;
	if (!node || !node->IsObject())
		return transform;
	auto number = [](const data::NodePtr& n, float fallback) -> float {
		if (!n)
			return fallback;
		if (n->IsInt())
			return float(n->intValue);
		if (n->IsFloat())
			return float(n->floatValue);
		return fallback;
	};
	auto vec3 = [&](const data::NodePtr& n, math::FVector3 fallback) -> math::FVector3 {
		if (!n || !n->IsArray() || n->GetSize() < 3)
			return fallback;
		return {number(n->At(0), fallback.x), number(n->At(1), fallback.y),
				number(n->At(2), fallback.z)};
	};
	transform.position = vec3(node->Get("position"), {});
	transform.SetEulerDegrees(vec3(node->Get("rotation"), {}));
	transform.scale = vec3(node->Get("scale"), {1.f, 1.f, 1.f});
	return transform;
}

// ── Component ────────────────────────────────────────────────────────────────

data::NodePtr Component::ToJson() const {
	auto node = data::Node::MakeObject();
	node->Set("type", data::Node::MakeString(type));
	if (!enabled)
		node->Set("enabled", data::Node::MakeBool(false));
	node->Set("props", props.ToJson());
	return node;
}

Component Component::FromJson(const data::NodePtr& node) {
	Component component;
	if (!node || !node->IsObject())
		return component;
	if (auto type = node->Get("type"); type && type->IsString())
		component.type = type->stringValue;
	if (auto enabled = node->Get("enabled"); enabled && enabled->IsBool())
		component.enabled = enabled->boolValue;
	component.props = PropertyMap::FromJson(node->Get("props"));
	return component;
}

// ── Node ─────────────────────────────────────────────────────────────────────

const Component* Node::FindComponent(const String& componentType) const noexcept {
	for (const Component& component : components)
		if (component.type == componentType)
			return &component;
	return nullptr;
}

Component* Node::FindComponent(const String& componentType) noexcept {
	for (Component& component : components)
		if (component.type == componentType)
			return &component;
	return nullptr;
}

bool Node::HasComponent(const String& componentType) const noexcept {
	return FindComponent(componentType) != nullptr;
}

Component& Node::SetComponent(Component component) {
	if (Component* existing = FindComponent(component.type)) {
		*existing = std::move(component);
		return *existing;
	}
	components.push_back(std::move(component));
	return components.back();
}

bool Node::RemoveComponent(const String& componentType) {
	for (size_t i = 0; i < components.size(); ++i)
		if (components[i].type == componentType) {
			components.erase(components.begin() + ptrdiff_t(i));
			return true;
		}
	return false;
}

const PropertyValue* Node::ComponentProperty(const String& componentType, const String& key) const noexcept {
	const Component* component = FindComponent(componentType);
	return component ? component->props.Find(key) : nullptr;
}

const PropertyValue* Node::Get(const String& key) const noexcept {
	return properties.Find(key);
}

data::NodePtr Node::ToJson() const {
	auto node = data::Node::MakeObject();
	node->Set("id", data::Node::MakeString(id.ToText()));
	node->Set("type", data::Node::MakeString(type));
	node->Set("name", data::Node::MakeString(name));
	node->Set("transform", transform.ToJson());
	if (!visible)
		node->Set("visible", data::Node::MakeBool(false));
	if (locked)
		node->Set("locked", data::Node::MakeBool(true));
	if (!components.empty()) {
		auto array = data::Node::MakeArray();
		for (const Component& component : components)
			array->Push(component.ToJson());
		node->Set("components", array);
	}
	if (!properties.IsEmpty())
		node->Set("properties", properties.ToJson());
	return node;
}

Node Node::FromJson(const data::NodePtr& json) {
	Node node;
	if (!json || !json->IsObject())
		return node;
	if (auto id = json->Get("id"); id && id->IsString())
		node.id = NodeId::FromText(id->stringValue);
	if (auto type = json->Get("type"); type && type->IsString() && !type->stringValue.IsEmpty())
		node.type = type->stringValue;
	if (auto name = json->Get("name"); name && name->IsString())
		node.name = name->stringValue;
	node.transform = Transform::FromJson(json->Get("transform"));
	if (auto visible = json->Get("visible"); visible && visible->IsBool())
		node.visible = visible->boolValue;
	if (auto locked = json->Get("locked"); locked && locked->IsBool())
		node.locked = locked->boolValue;
	if (auto array = json->Get("components"); array && array->IsArray())
		for (size_t i = 0; i < array->GetSize(); ++i)
			node.components.push_back(Component::FromJson(array->At(i)));
	node.properties = PropertyMap::FromJson(json->Get("properties"));
	return node;
}

} // namespace scene
