// Définitions de scene/property.hpp
#include "scene/property.hpp"

namespace scene {

// ── NodeId ───────────────────────────────────────────────────────────────────

String NodeId::ToText() const {
	return Valid() ? String::Format("%u:%u", index, generation) : String("-");
}

NodeId NodeId::FromText(const String& text) {
	int colon = text.IndexOf(':');
	if (colon <= 0)
		return NodeId{};
	NodeId id;
	id.index = uint32_t(text.Substr(0, size_t(colon)).TryParseInt().UnwrapOr(int64_t(INVALID)));
	id.generation = uint32_t(text.Substr(size_t(colon) + 1).TryParseInt().UnwrapOr(0));
	return id;
}

} // namespace scene

namespace scene {

const char* PropertyTypeName(PropertyType type) noexcept {
	switch (type) {
	case PropertyType::NIL:
		return "nil";
	case PropertyType::BOOL:
		return "bool";
	case PropertyType::INT:
		return "int";
	case PropertyType::FLOAT:
		return "float";
	case PropertyType::STRING:
		return "string";
	case PropertyType::VEC2:
		return "vec2";
	case PropertyType::VEC3:
		return "vec3";
	case PropertyType::QUAT:
		return "quat";
	case PropertyType::COLOR:
		return "color";
	case PropertyType::RESOURCE:
		return "resource";
	case PropertyType::NODE_REF:
		return "node";
	case PropertyType::ARRAY:
		return "array";
	case PropertyType::DICT:
		return "dict";
	}
	return "nil";
}

Option<PropertyType> PropertyTypeFromName(const String& name) {
	struct Entry {
		const char* name;
		PropertyType type;
	};
	static constexpr Entry TABLE[] = {
		{"nil", PropertyType::NIL},		  {"bool", PropertyType::BOOL},
		{"int", PropertyType::INT},		  {"float", PropertyType::FLOAT},
		{"string", PropertyType::STRING}, {"vec2", PropertyType::VEC2},
		{"vec3", PropertyType::VEC3},	  {"quat", PropertyType::QUAT},
		{"color", PropertyType::COLOR},	  {"resource", PropertyType::RESOURCE},
		{"node", PropertyType::NODE_REF}, {"array", PropertyType::ARRAY},
		{"dict", PropertyType::DICT},
	};
	for (const Entry& entry : TABLE)
		if (name == entry.name)
			return Some(entry.type);
	return NONE;
}

// ── PropertyMap ──────────────────────────────────────────────────────────────

const std::vector<std::pair<String, PropertyValue>>& PropertyMap::Entries() const noexcept {
	return m_entries;
}

std::vector<std::pair<String, PropertyValue>>& PropertyMap::Entries() noexcept {
	return m_entries;
}

// ── PropertyValue ────────────────────────────────────────────────────────────

PropertyValue PropertyValue::Bool(bool v) {
	PropertyValue p;
	p.m_type = PropertyType::BOOL;
	p.m_bool = v;
	return p;
}

PropertyValue PropertyValue::Int(int64_t v) {
	PropertyValue p;
	p.m_type = PropertyType::INT;
	p.m_int = v;
	return p;
}

PropertyValue PropertyValue::Float(double v) {
	PropertyValue p;
	p.m_type = PropertyType::FLOAT;
	p.m_float = v;
	return p;
}

PropertyValue PropertyValue::Str(String v) {
	PropertyValue p;
	p.m_type = PropertyType::STRING;
	p.m_string = std::move(v);
	return p;
}

PropertyValue PropertyValue::Vec2(const math::FVector2& v) {
	PropertyValue p;
	p.m_type = PropertyType::VEC2;
	p.m_vec = {v.x, v.y, 0.f, 0.f};
	return p;
}

PropertyValue PropertyValue::Vec3(const math::FVector3& v) {
	PropertyValue p;
	p.m_type = PropertyType::VEC3;
	p.m_vec = {v.x, v.y, v.z, 0.f};
	return p;
}

PropertyValue PropertyValue::Quat(const math::FQuaternion& q) {
	PropertyValue p;
	p.m_type = PropertyType::QUAT;
	p.m_vec = {q.x, q.y, q.z, q.w};
	return p;
}

PropertyValue PropertyValue::Resource(String path) {
	PropertyValue p;
	p.m_type = PropertyType::RESOURCE;
	p.m_string = std::move(path);
	return p;
}

PropertyValue PropertyValue::NodeRef(NodeId id) {
	PropertyValue p;
	p.m_type = PropertyType::NODE_REF;
	p.m_node = id;
	return p;
}

PropertyValue PropertyValue::Array(std::vector<PropertyValue> items) {
	PropertyValue p;
	p.m_type = PropertyType::ARRAY;
	p.m_array = std::move(items);
	return p;
}

PropertyValue PropertyValue::Dict(PropertyMap map) {
	PropertyValue p;
	p.m_type = PropertyType::DICT;
	p.m_dict = std::make_shared<PropertyMap>(std::move(map));
	return p;
}

bool PropertyValue::IsNumber() const noexcept {
	return m_type == PropertyType::INT || m_type == PropertyType::FLOAT;
}

bool PropertyValue::AsBool(bool fallback) const noexcept {
	switch (m_type) {
	case PropertyType::BOOL:
		return m_bool;
	case PropertyType::INT:
		return m_int != 0;
	case PropertyType::FLOAT:
		return m_float != 0.0;
	default:
		return fallback;
	}
}

int64_t PropertyValue::AsInt(int64_t fallback) const noexcept {
	switch (m_type) {
	case PropertyType::INT:
		return m_int;
	case PropertyType::FLOAT:
		return int64_t(m_float);
	case PropertyType::BOOL:
		return m_bool ? 1 : 0;
	default:
		return fallback;
	}
}

float PropertyValue::AsFloat(float fallback) const noexcept {
	switch (m_type) {
	case PropertyType::FLOAT:
		return float(m_float);
	case PropertyType::INT:
		return float(m_int);
	case PropertyType::BOOL:
		return m_bool ? 1.f : 0.f;
	default:
		return fallback;
	}
}

String PropertyValue::AsString(const char* fallback) const {
	if (m_type == PropertyType::STRING || m_type == PropertyType::RESOURCE)
		return m_string;
	return String(fallback);
}

math::FVector2 PropertyValue::AsVec2(math::FVector2 fallback) const noexcept {
	return m_type == PropertyType::VEC2 ? math::FVector2{m_vec.x, m_vec.y} : fallback;
}

math::FVector3 PropertyValue::AsVec3(math::FVector3 fallback) const noexcept {
	return m_type == PropertyType::VEC3 ? math::FVector3{m_vec.x, m_vec.y, m_vec.z} : fallback;
}

math::FQuaternion PropertyValue::AsQuat(math::FQuaternion fallback) const noexcept {
	return m_type == PropertyType::QUAT ? math::FQuaternion{m_vec.x, m_vec.y, m_vec.z, m_vec.w}
										: fallback;
}

NodeId PropertyValue::AsNodeRef() const noexcept {
	return m_type == PropertyType::NODE_REF ? m_node : NodeId{};
}

const PropertyMap* PropertyValue::AsDict() const noexcept {
	return m_dict ? m_dict.get() : nullptr;
}

data::NodePtr PropertyValue::ToJson() const {
	auto node = data::Node::MakeObject();
	node->Set("type", data::Node::MakeString(String(PropertyTypeName(m_type))));
	switch (m_type) {
	case PropertyType::NIL:
		break;
	case PropertyType::BOOL:
		node->Set("value", data::Node::MakeBool(m_bool));
		break;
	case PropertyType::INT:
		node->Set("value", data::Node::MakeInt(m_int));
		break;
	case PropertyType::FLOAT:
		node->Set("value", data::Node::MakeFloat(m_float));
		break;
	case PropertyType::STRING:
	case PropertyType::RESOURCE:
		node->Set("value", data::Node::MakeString(m_string));
		break;
	case PropertyType::VEC2:
	case PropertyType::VEC3:
	case PropertyType::QUAT:
	case PropertyType::COLOR: {
		auto array = data::Node::MakeArray();
		const int count =
			m_type == PropertyType::VEC2 ? 2 : (m_type == PropertyType::VEC3 ? 3 : 4);
		const float components[4] = {m_vec.x, m_vec.y, m_vec.z, m_vec.w};
		for (int i = 0; i < count; ++i)
			array->Push(data::Node::MakeFloat(double(components[i])));
		node->Set("value", array);
		break;
	}
	case PropertyType::NODE_REF:
		node->Set("value", data::Node::MakeString(m_node.ToText()));
		break;
	case PropertyType::ARRAY: {
		auto array = data::Node::MakeArray();
		for (const PropertyValue& item : m_array)
			array->Push(item.ToJson());
		node->Set("value", array);
		break;
	}
	case PropertyType::DICT:
		node->Set("value", m_dict ? m_dict->ToJson() : data::Node::MakeObject());
		break;
	}
	return node;
}

PropertyValue PropertyValue::FromJson(const data::NodePtr& node) {
	if (!node || !node->IsObject())
		return PropertyValue{};
	auto typeNode = node->Get("type");
	PropertyType type = PropertyTypeFromName(
							typeNode && typeNode->IsString() ? typeNode->stringValue : String())
							.UnwrapOr(PropertyType::NIL);
	auto value = node->Get("value");
	auto number = [](const data::NodePtr& n, float fallback = 0.f) -> float {
		if (!n)
			return fallback;
		if (n->IsInt())
			return float(n->intValue);
		if (n->IsFloat())
			return float(n->floatValue);
		return fallback;
	};
	switch (type) {
	case PropertyType::NIL:
		return PropertyValue{};
	case PropertyType::BOOL:
		return Bool(value && value->IsBool() ? value->boolValue : false);
	case PropertyType::INT:
		return Int(value && value->IsInt() ? value->intValue : int64_t(number(value)));
	case PropertyType::FLOAT:
		return Float(double(number(value)));
	case PropertyType::STRING:
		return Str(value && value->IsString() ? value->stringValue : String());
	case PropertyType::RESOURCE:
		return Resource(value && value->IsString() ? value->stringValue : String());
	case PropertyType::VEC2:
	case PropertyType::VEC3:
	case PropertyType::QUAT:
	case PropertyType::COLOR: {
		float c[4] = {0.f, 0.f, 0.f, type == PropertyType::QUAT ? 1.f : 255.f};
		if (value && value->IsArray())
			for (size_t i = 0; i < value->GetSize() && i < 4; ++i)
				c[i] = number(value->At(i));
		if (type == PropertyType::VEC2)
			return Vec2({c[0], c[1]});
		if (type == PropertyType::VEC3)
			return Vec3({c[0], c[1], c[2]});
		if (type == PropertyType::QUAT)
			return Quat(math::FQuaternion{c[0], c[1], c[2], c[3]});
		auto channel = [](float v) -> uint8_t {
			return uint8_t(v < 0.f ? 0.f : (v > 255.f ? 255.f : v));
		};
		return Color(sdl3::Color{channel(c[0]), channel(c[1]), channel(c[2]), channel(c[3])});
	}
	case PropertyType::NODE_REF:
		return NodeRef(
			NodeId::FromText(value && value->IsString() ? value->stringValue : String()));
	case PropertyType::ARRAY: {
		std::vector<PropertyValue> items;
		if (value && value->IsArray())
			for (size_t i = 0; i < value->GetSize(); ++i)
				items.push_back(FromJson(value->At(i)));
		return Array(std::move(items));
	}
	case PropertyType::DICT:
		return Dict(PropertyMap::FromJson(value));
	}
	return PropertyValue{};
}

String PropertyValue::ToDisplayString() const {
	switch (m_type) {
	case PropertyType::NIL:
		return String("nil");
	case PropertyType::BOOL:
		return String(m_bool ? "true" : "false");
	case PropertyType::INT:
		return String::From(static_cast<long long>(m_int));
	case PropertyType::FLOAT:
		return String::From(m_float, 3);
	case PropertyType::STRING:
	case PropertyType::RESOURCE:
		return m_string;
	case PropertyType::VEC2:
		return String::Format("(%.3f, %.3f)", double(m_vec.x), double(m_vec.y));
	case PropertyType::VEC3:
		return String::Format("(%.3f, %.3f, %.3f)", double(m_vec.x), double(m_vec.y),
							  double(m_vec.z));
	case PropertyType::QUAT:
		return String::Format("(%.3f, %.3f, %.3f, %.3f)", double(m_vec.x), double(m_vec.y),
							  double(m_vec.z), double(m_vec.w));
	case PropertyType::COLOR:
		return String::Format("#%02X%02X%02X", int(m_vec.x), int(m_vec.y), int(m_vec.z));
	case PropertyType::NODE_REF:
		return String::Format("node(%s)", m_node.ToText().CStr());
	case PropertyType::ARRAY:
		return String::Format("[%zu]", m_array.size());
	case PropertyType::DICT:
		return String::Format("{%zu}", m_dict ? m_dict->Size() : size_t(0));
	}
	return String("nil");
}

const PropertyValue* PropertyMap::Find(const String& key) const noexcept {
	for (const auto& entry : m_entries)
		if (entry.first == key)
			return &entry.second;
	return nullptr;
}

PropertyValue* PropertyMap::Find(const String& key) noexcept {
	for (auto& entry : m_entries)
		if (entry.first == key)
			return &entry.second;
	return nullptr;
}

void PropertyMap::Set(const String& key, PropertyValue value) {
	if (PropertyValue* found = Find(key)) {
		*found = std::move(value);
		return;
	}
	m_entries.push_back({key, std::move(value)});
}

bool PropertyMap::Remove(const String& key) {
	for (size_t i = 0; i < m_entries.size(); ++i)
		if (m_entries[i].first == key) {
			m_entries.erase(m_entries.begin() + ptrdiff_t(i));
			return true;
		}
	return false;
}

bool PropertyMap::Rename(const String& key, const String& newKey) {
	if (key == newKey || Find(newKey))
		return false;
	if (PropertyValue* found = Find(key)) {
		for (auto& entry : m_entries)
			if (entry.first == key) {
				entry.first = newKey;
				return true;
			}
		(void)found;
	}
	return false;
}

std::vector<String> PropertyMap::Keys() const {
	std::vector<String> keys;
	keys.reserve(m_entries.size());
	for (const auto& entry : m_entries)
		keys.push_back(entry.first);
	return keys;
}

data::NodePtr PropertyMap::ToJson() const {
	auto node = data::Node::MakeObject();
	for (const auto& [key, value] : m_entries)
		node->Set(key, value.ToJson());
	return node;
}

PropertyMap PropertyMap::FromJson(const data::NodePtr& node) {
	PropertyMap map;
	if (!node || !node->IsObject())
		return map;
	for (const String& key : node->Keys())
		map.Set(key, PropertyValue::FromJson(node->Get(key)));
	return map;
}

} // namespace scene
