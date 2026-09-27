// Définitions de data/script/script_value.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/script/script_value.hpp"

namespace data::script {

const char * NumberTypeName(NumberType t) noexcept {
	constexpr const char *NAMES[] = {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64"};
	return NAMES[static_cast<size_t>(t)];
}

Option<NumberType> NumberTypeFromName(StringView name) noexcept {
	for (int i = 0; i <= static_cast<int>(NumberType::F64); ++i)
		if (name == StringView(NumberTypeName(static_cast<NumberType>(i))))
			return Some(static_cast<NumberType>(i));
	return NONE;
}

// ── Value ────────────────────────────────────────────────────────────────────

Value Value::Boolean(bool v) {
	Value value;
	value.m_kind = Kind::BOOLEAN;
	value.m_boolean = v;
	return value;
}

Value Value::Float(double v, NumberType type) {
	Value value;
	value.m_kind = Kind::NUMBER;
	value.m_numberType = type == NumberType::F32 ? NumberType::F32 : NumberType::F64;
	value.m_number = type == NumberType::F32 ? double(float(v)) : v;
	return value;
}

Value Value::Int(int64_t v, NumberType type) {
	Value value;
	value.m_kind = Kind::NUMBER;
	value.m_numberType = IsIntegerType(type) && IsSignedType(type) ? type : NumberType::I64;
	value.m_int = v;
	return value;
}

Value Value::UInt(uint64_t v, NumberType type) {
	Value value;
	value.m_kind = Kind::NUMBER;
	value.m_numberType = IsIntegerType(type) && !IsSignedType(type) ? type : NumberType::U64;
	value.m_uint = v;
	return value;
}

Value Value::Str(String v) {
	Value value;
	value.m_kind = Kind::STRING;
	value.m_text = std::move(v);
	return value;
}

Value Value::List(std::shared_ptr<ListObject> v) {
	Value value;
	value.m_kind = Kind::LIST;
	value.m_list = std::move(v);
	return value;
}

Value Value::Map(std::shared_ptr<MapObject> v) {
	Value value;
	value.m_kind = Kind::MAP;
	value.m_map = std::move(v);
	return value;
}

Value Value::Function(std::shared_ptr<FunctionObject> v) {
	Value value;
	value.m_kind = Kind::FUNCTION;
	value.m_function = std::move(v);
	return value;
}

Value Value::Native(std::shared_ptr<NativeObject> v) {
	Value value;
	value.m_kind = Kind::NATIVE;
	value.m_native = std::move(v);
	return value;
}

Value Value::Namespace(std::shared_ptr<NamespaceObject> v) {
	Value value;
	value.m_kind = Kind::NAMESPACE;
	value.m_namespace = std::move(v);
	return value;
}

Value Value::Class(std::shared_ptr<ClassObject> v) {
	Value value;
	value.m_kind = Kind::CLASS;
	value.m_class = std::move(v);
	return value;
}

Value Value::Instance(std::shared_ptr<InstanceObject> v) {
	Value value;
	value.m_kind = Kind::INSTANCE;
	value.m_instance = std::move(v);
	return value;
}

Value Value::Future(std::shared_ptr<FutureObject> v) {
	Value value;
	value.m_kind = Kind::FUTURE;
	value.m_future = std::move(v);
	return value;
}

Value Value::Host(std::shared_ptr<HostObject> v) {
	Value value;
	value.m_kind = Kind::HOST;
	value.m_host = std::move(v);
	return value;
}

Value Value::Type(std::shared_ptr<TypeObject> v) {
	Value value;
	value.m_kind = Kind::TYPE;
	value.m_type = std::move(v);
	return value;
}

double Value::AsNumber() const noexcept {
	if (!IsIntegerType(m_numberType))
		return m_number;
	return IsSignedType(m_numberType) ? double(m_int) : double(m_uint);
}

int64_t Value::AsInt64() const noexcept {
	if (!IsIntegerType(m_numberType))
		return m_number >= 9.2233720368547758e18 ? INT64_MAX
			   : m_number <= -9.2233720368547758e18 ? INT64_MIN
													 : static_cast<int64_t>(m_number);
	if (IsSignedType(m_numberType))
		return m_int;
	return m_uint > uint64_t(INT64_MAX) ? INT64_MAX : static_cast<int64_t>(m_uint);
}

uint64_t Value::AsUInt64() const noexcept {
	if (!IsIntegerType(m_numberType))
		return m_number <= 0.0 ? 0 : (m_number >= 1.8446744073709552e19 ? UINT64_MAX : static_cast<uint64_t>(m_number));
	if (IsSignedType(m_numberType))
		return m_int < 0 ? 0 : static_cast<uint64_t>(m_int);
	return m_uint;
}

bool Value::IsTruthy() const noexcept {
	if (m_kind == Kind::NIL)
		return false;
	if (m_kind == Kind::BOOLEAN)
		return m_boolean;
	return true;
}

const char * Value::TypeName() const noexcept {
	switch (m_kind) {
		case Kind::NIL:
			return "nil";
		case Kind::BOOLEAN:
			return "bool";
		case Kind::NUMBER:
			return NumberTypeName(m_numberType);
		case Kind::STRING:
			return "string";
		case Kind::LIST:
			return "list";
		case Kind::MAP:
			return "map";
		case Kind::FUNCTION:
			return "function";
		case Kind::NATIVE:
			return "native";
		case Kind::NAMESPACE:
			return "namespace";
		case Kind::CLASS:
			return m_class && IsInterfaceClass(*m_class) ? "interface" : "class";
		case Kind::INSTANCE:
			return "instance";
		case Kind::FUTURE:
			return "future";
		case Kind::HOST:
			return HostTypeName(m_host.get());
		case Kind::TYPE:
			return "type";
	}
	return "?";
}

String Value::ToDisplayString() const {
	switch (m_kind) {
		case Kind::NIL:
			return String("nil");
		case Kind::BOOLEAN:
			return String(m_boolean ? "true" : "false");
		case Kind::NUMBER:
			if (IsIntegerType(m_numberType))
				return IsSignedType(m_numberType) ? String::From(static_cast<long long>(m_int))
												  : String::From(static_cast<unsigned long long>(m_uint));
			return NumberToString(m_number);
		case Kind::STRING:
			return m_text;
		case Kind::LIST: {
			// Copie sous verrou, puis mise en forme SANS lui : un élément
			// peut être une autre liste (verrou distinct).
			String out("[");
			if (m_list) {
				const std::vector<Value> items = m_list->Snapshot();
				for (size_t i = 0; i < items.size(); ++i) {
					if (i > 0)
						out.Concat(", ");
					out.Concat(items[i].ToDisplayString());
				}
			}
			out.Concat("]");
			return out;
		}
		case Kind::MAP: {
			String out("{");
			if (m_map) {
				const auto entries = m_map->Snapshot();
				for (size_t i = 0; i < entries.size(); ++i) {
					if (i > 0)
						out.Concat(", ");
					out.Concat(entries[i].first);
					out.Concat(": ");
					out.Concat(entries[i].second.ToDisplayString());
				}
			}
			out.Concat("}");
			return out;
		}
		case Kind::FUNCTION:
			return String::Format("<fn %s>", (m_function && !m_function->def->name.IsEmpty())
												  ? m_function->def->name.CStr()
												  : "anonyme");
		case Kind::NATIVE:
			return String::Format("<native %s>", m_native ? m_native->name.CStr() : "?");
		case Kind::NAMESPACE:
			return String::Format("<namespace %s>", m_namespace ? m_namespace->name.CStr() : "?");
		case Kind::CLASS:
			return m_class ? DescribeClass(*m_class) : String("<class ?>");
		case Kind::INSTANCE:
			return m_instance ? DescribeInstance(*m_instance) : String("<instance ?>");
		case Kind::FUTURE:
			return m_future ? DescribeFuture(*m_future) : String("<future ?>");
		case Kind::HOST:
			return DescribeHost(m_host.get());
		case Kind::TYPE:
			return m_type ? DescribeType(*m_type) : String("<type ?>");
	}
	return String("?");
}

String Value::NumberToString(double v) {
	if (std::isnan(v))
		return String("nan");
	if (std::isinf(v))
		return String(v > 0 ? "inf" : "-inf");
	if (v == std::floor(v) && std::fabs(v) < 1e15)
		return String::From(static_cast<long long>(v));
	return String::Format("%.6g", v);
}

FieldSlot * FindField(std::vector<FieldSlot> &fields, const String &name) noexcept {
	for (FieldSlot &field : fields)
		if (field.name == name)
			return &field;
	return nullptr;
}

// ── FieldSet ─────────────────────────────────────────────────────────────────

Option<Value> FieldSet::Get(const String &name) const {
	std::lock_guard<std::mutex> lock(mutex);
	for (const FieldSlot &slot : slots)
		if (slot.name == name)
			return Some(slot.value);
	return NONE;
}

FieldSet::WriteResult FieldSet::Set(const String &name, Value value, bool create) {
	std::lock_guard<std::mutex> lock(mutex);
	if (FieldSlot *slot = FindField(slots, name)) {
		if (slot->constant)
			return WriteResult::CONSTANT;
		slot->value = std::move(value);
		return WriteResult::OK;
	}
	if (!create)
		return WriteResult::MISSING;
	slots.push_back(FieldSlot{name, std::move(value), false, nullptr});
	return WriteResult::CREATED;
}

void FieldSet::Initialize(const String &name, Value value, bool constant, TypeRef type) {
	std::lock_guard<std::mutex> lock(mutex);
	if (FieldSlot *slot = FindField(slots, name)) {
		slot->value = std::move(value);
		slot->constant = constant;
		slot->type = std::move(type);
		return;
	}
	slots.push_back(FieldSlot{name, std::move(value), constant, std::move(type)});
}

TypeRef FieldSet::TypeOf(const String &name) const {
	std::lock_guard<std::mutex> lock(mutex);
	for (const FieldSlot &slot : slots)
		if (slot.name == name)
			return slot.type;
	return nullptr;
}

bool FieldSet::Has(const String &name) const {
	std::lock_guard<std::mutex> lock(mutex);
	for (const FieldSlot &slot : slots)
		if (slot.name == name)
			return true;
	return false;
}

std::vector<FieldSlot> FieldSet::Snapshot() const {
	std::lock_guard<std::mutex> lock(mutex);
	return slots;
}

FieldSet::WriteResult FieldSet::CompareAndSet(const String &name, const Value &expected, Value value) {
	std::lock_guard<std::mutex> lock(mutex);
	FieldSlot *slot = FindField(slots, name);
	if (!slot)
		return WriteResult::MISSING;
	if (slot->constant)
		return WriteResult::CONSTANT;
	if (!slot->value.Equals(expected))
		return WriteResult::MISSING; // modifié entre-temps : l'appelant recommence
	slot->value = std::move(value);
	return WriteResult::OK;
}

// ── ClassObject ──────────────────────────────────────────────────────────────

bool ClassObject::IsSubtypeOf(const ClassObject &other) const noexcept {
	if (this == &other)
		return true;
	for (const auto &iface : interfaces)
		if (iface && iface->IsSubtypeOf(other))
			return true;
	return superclass && superclass->IsSubtypeOf(other);
}

// ── InstanceObject ───────────────────────────────────────────────────────────

const std::vector<Value> * InstanceObject::TypeArgsOf(const ClassObject *level) const noexcept {
	for (const auto &entry : typeArgs)
		if (entry.first == level)
			return &entry.second;
	return nullptr;
}

// ── FinalizerQueue ───────────────────────────────────────────────────────────

FinalizerQueue::~FinalizerQueue() {
	closed = true;
	std::vector<std::shared_ptr<InstanceObject>> released;
	released.swap(pending);
}

FinalizerQueue & PendingFinalizers() noexcept {
	thread_local FinalizerQueue queue;
	return queue;
}

InstanceObject::~InstanceObject() {
	if (finalized || !klass || !klass->hasDeinit || !klass->ownerAlive || !klass->ownerAlive->load())
		return;
	FinalizerQueue &queue = PendingFinalizers();
	if (queue.closed)
		return;
	auto ghost = std::make_shared<InstanceObject>();
	ghost->klass = klass;
	ghost->finalized = true;
	{
		std::lock_guard<std::mutex> lock(fields.mutex);
		ghost->fields.slots = std::move(fields.slots);
	}
	ghost->typeArgs = std::move(typeArgs);
	queue.pending.push_back(std::move(ghost));
}

// ── TypeObject ───────────────────────────────────────────────────────────────

bool TypeObject::IsBound() const {
	std::lock_guard<std::mutex> lock(mutex);
	return type || klass || hostType;
}

// ── HostType ─────────────────────────────────────────────────────────────────

const HostMethod * HostType::FindMethod(const String &methodName) const noexcept {
	for (const HostMethod &method : methods)
		if (method.name == methodName)
			return &method;
	return nullptr;
}

const Value * HostType::FindStatic(const String &memberName) const noexcept {
	for (const auto &entry : statics)
		if (entry.first == memberName)
			return &entry.second;
	return nullptr;
}

const char * HostTypeName(const HostObject *object) noexcept {
	return object && object->type ? object->type->name.CStr() : "host";
}

bool HostEquals(const HostObject *a, const HostObject *b) {
	if (!a || !b || a->type != b->type || !a->type->equals)
		return false;
	return a->type->equals(*a, *b);
}

String DescribeHost(const HostObject *object) {
	if (!object || !object->type)
		return String("<host ?>");
	if (object->type->display)
		return object->type->display(*object);
	return String::Format("<%s>", object->type->name.CStr());
}

String DescribeType(const TypeObject &type) {
	std::lock_guard<std::mutex> lock(type.mutex);
	String bound = type.type        ? type.type->ToString()
				   : type.klass     ? DescribeClass(*type.klass)
				   : type.hostType  ? type.hostType->name
									: String("?");
	return String::Format("<type %s = %s>", type.name.CStr(), bound.CStr());
}

// ── FutureObject ─────────────────────────────────────────────────────────────

void FutureObject::Complete(Result<Value, ScriptError> result) {
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (result.IsOk()) {
			value = result.Value();
			state = State::DONE;
		} else {
			error = result.Error();
			state = State::FAILED;
		}
	}
	ready.notify_all();
}

FutureObject::State FutureObject::GetState() const {
	std::lock_guard<std::mutex> lock(mutex);
	return state;
}

String DescribeFuture(const FutureObject &future) {
	std::lock_guard<std::mutex> lock(future.mutex);
	switch (future.state) {
		case FutureObject::State::PENDING:
			return String("<future en cours>");
		case FutureObject::State::DONE:
			return String::Format("<future : %s>", future.value.ToDisplayString().CStr());
		case FutureObject::State::FAILED:
			return String::Format("<future en échec : %s>", future.error.message.CStr());
	}
	return String("<future>");
}

String DescribeClass(const ClassObject &klass) {
	return String::Format("<%s %s>", klass.IsInterface() ? "interface" : "class", klass.Name().CStr());
}

String DescribeInstance(const InstanceObject &instance) {
	// Valeur d'un type énuméré : `Couleur.ROUGE`.
	if (instance.klass && instance.klass->def->isEnum) {
		Option<Value> name = instance.fields.Get(String("name"));
		return instance.klass->Name() + String(".") + (name.IsSome() ? name.Value().ToDisplayString() : String("?"));
	}
	String out = instance.klass ? instance.klass->Name() : String("?");
	out.Concat("{");
	const std::vector<FieldSlot> fields = instance.fields.Snapshot();
	for (size_t i = 0; i < fields.size(); ++i) {
		if (i > 0)
			out.Concat(", ");
		out.Concat(fields[i].name);
		out.Concat(": ");
		const Value &value = fields[i].value;
		out.Concat(value.IsInstance() ? String("…") : value.ToDisplayString());
	}
	out.Concat("}");
	return out;
}

Value * MapObject::Find(const String &key) noexcept {
	for (auto &entry : entries)
		if (entry.first == key)
			return &entry.second;
	return nullptr;
}

const Value * MapObject::Find(const String &key) const noexcept {
	for (const auto &entry : entries)
		if (entry.first == key)
			return &entry.second;
	return nullptr;
}

Option<Value> MapObject::Get(const String &key) const {
	std::lock_guard<std::mutex> lock(mutex);
	if (const Value *found = Find(key))
		return Some(*found);
	return NONE;
}

void MapObject::SetKey(const String &key, Value value) {
	std::lock_guard<std::mutex> lock(mutex);
	if (Value *existing = Find(key)) {
		*existing = std::move(value);
		return;
	}
	entries.emplace_back(key, std::move(value));
}

size_t MapObject::Size() const {
	std::lock_guard<std::mutex> lock(mutex);
	return entries.size();
}

std::vector<std::pair<String, Value>> MapObject::Snapshot() const {
	std::lock_guard<std::mutex> lock(mutex);
	return entries;
}

bool MapObject::CompareAndSet(const String &key, const Value &expected, Value value) {
	std::lock_guard<std::mutex> lock(mutex);
	Value *current = Find(key);
	if (!(current ? current->Equals(expected) : expected.IsNil()))
		return false;
	if (current)
		*current = std::move(value);
	else
		entries.emplace_back(key, std::move(value));
	return true;
}

size_t ListObject::Size() const {
	std::lock_guard<std::mutex> lock(mutex);
	return items.size();
}

Option<Value> ListObject::At(size_t index) const {
	std::lock_guard<std::mutex> lock(mutex);
	if (index >= items.size())
		return NONE;
	return Some(items[index]);
}

bool ListObject::Set(size_t index, Value value) {
	std::lock_guard<std::mutex> lock(mutex);
	if (index >= items.size())
		return false;
	items[index] = std::move(value);
	return true;
}

void ListObject::Push(Value value) {
	std::lock_guard<std::mutex> lock(mutex);
	items.push_back(std::move(value));
}

std::vector<Value> ListObject::Snapshot() const {
	std::lock_guard<std::mutex> lock(mutex);
	return items;
}

void ListObject::Replace(std::vector<Value> values) {
	std::lock_guard<std::mutex> lock(mutex);
	items = std::move(values);
}

bool ListObject::CompareAndSet(size_t index, const Value &expected, Value value) {
	std::lock_guard<std::mutex> lock(mutex);
	if (index >= items.size() || !items[index].Equals(expected))
		return false;
	items[index] = std::move(value);
	return true;
}

bool MapObject::RemoveKey(const String &key) {
	std::lock_guard<std::mutex> lock(mutex);
	for (size_t i = 0; i < entries.size(); ++i) {
		if (entries[i].first == key) {
			entries.erase(entries.begin() + static_cast<ptrdiff_t>(i));
			return true;
		}
	}
	return false;
}

Value ValueFromNode(const NodePtr &node) {
	if (!node)
		return Value::Nil();
	switch (node->type) {
		case NodeType::NONE:
			return Value::Nil();
		case NodeType::STRING:
			return Value::Str(node->stringValue);
		case NodeType::BOOL:
			return Value::Boolean(node->boolValue);
		case NodeType::INT:
			return Value::Int(node->intValue);
		case NodeType::FLOAT:
			return Value::Number(node->floatValue);
		case NodeType::ARRAY: {
			auto list = std::make_shared<ListObject>();
			list->items.reserve(node->GetSize());
			for (size_t i = 0; i < node->GetSize(); ++i)
				list->items.push_back(ValueFromNode(node->At(i)));
			return Value::List(std::move(list));
		}
		case NodeType::OBJECT: {
			auto map = std::make_shared<MapObject>();
			for (const String &key : node->Keys())
				map->entries.emplace_back(key, ValueFromNode(node->Get(key)));
			return Value::Map(std::move(map));
		}
	}
	return Value::Nil();
}

NodePtr NodeFromValue(const Value &value) {
	switch (value.GetKind()) {
		case Value::Kind::NIL:
			return Node::MakeNone();
		case Value::Kind::BOOLEAN:
			return Node::MakeBool(value.AsBoolean());
		case Value::Kind::NUMBER: {
			// Entier -> INT, flottant -> FLOAT : chacun revient avec son genre
			// (l'encodeur JSON écrit toujours la virgule d'un FLOAT).
			if (value.IsInteger() && (IsSignedType(value.GetNumberType()) || value.AsUInt64() <= uint64_t(INT64_MAX)))
				return Node::MakeInt(value.AsInt64());
			return Node::MakeFloat(value.AsNumber());
		}
		case Value::Kind::STRING:
			return Node::MakeString(value.AsString());
		case Value::Kind::LIST: {
			auto arr = Node::MakeArray();
			if (value.AsList())
				for (const Value &item : value.AsList()->Snapshot())
					arr->Push(NodeFromValue(item));
			return arr;
		}
		case Value::Kind::MAP: {
			auto obj = Node::MakeObject();
			if (value.AsMap())
				for (const auto &entry : value.AsMap()->Snapshot())
					obj->Set(entry.first, NodeFromValue(entry.second));
			return obj;
		}
		case Value::Kind::INSTANCE: {
			// Valeur d'un type énuméré : son nom.
			if (value.AsInstance() && value.AsInstance()->klass->def->isEnum) {
				Option<Value> name = value.AsInstance()->fields.Get(String("name"));
				return Node::MakeString(name.IsSome() ? name.Value().ToDisplayString() : String());
			}
			// Les champs de l'instance, comme une table (export JSON d'un objet).
			auto obj = Node::MakeObject();
			if (value.AsInstance())
				for (const FieldSlot &field : value.AsInstance()->fields.Snapshot())
					obj->Set(field.name, NodeFromValue(field.value));
			return obj;
		}
		case Value::Kind::FUTURE:
			return Node::MakeNone();
		case Value::Kind::CLASS:
			return value.AsClass() ? Node::MakeString(value.AsClass()->Name()) : Node::MakeNone();
		case Value::Kind::HOST: {
			// Un conteneur de l'hôte : ses éléments ; sinon son texte.
			const std::shared_ptr<HostObject> &host = value.AsHost();
			if (host && host->type->items) {
				auto arr = Node::MakeArray();
				for (const Value &item : host->type->items(*host))
					arr->Push(NodeFromValue(item));
				return arr;
			}
			return Node::MakeString(value.ToDisplayString());
		}
		case Value::Kind::TYPE:
			return Node::MakeString(value.ToDisplayString());
		case Value::Kind::FUNCTION:
		case Value::Kind::NATIVE:
		case Value::Kind::NAMESPACE:
			return Node::MakeNone();
	}
	return Node::MakeNone();
}

namespace numeric {

Wide ToWide(const Value &v) noexcept {
	return IsSignedType(v.GetNumberType()) ? Wide(v.AsInt64()) : Wide(v.AsUInt64());
}

Wide LowOf(NumberType t) noexcept {
	return IsSignedType(t) ? -(Wide(1) << (BitsOf(t) - 1)) : Wide(0);
}

Wide HighOf(NumberType t) noexcept {
	return IsSignedType(t) ? (Wide(1) << (BitsOf(t) - 1)) - 1 : (Wide(1) << BitsOf(t)) - 1;
}

Value FromWide(Wide v, NumberType t) {
	return IsSignedType(t) ? Value::Int(static_cast<int64_t>(v), t) : Value::UInt(static_cast<uint64_t>(v), t);
}

String WideToString(Wide v) {
	if (v >= Wide(INT64_MIN) && v <= Wide(INT64_MAX))
		return String::From(static_cast<long long>(static_cast<int64_t>(v)));
	if (v > 0 && v <= Wide(UINT64_MAX))
		return String::From(static_cast<unsigned long long>(static_cast<uint64_t>(v)));
	return String(v < 0 ? "< -2^64" : "> 2^64");
}

NumberType CommonInteger(const Value &a, const Value &b, Wide r) noexcept {
	const NumberType ta = a.GetNumberType(), tb = b.GetNumberType();
	if (BitsOf(ta) != BitsOf(tb))
		return BitsOf(ta) > BitsOf(tb) ? ta : tb;
	if (IsSignedType(ta) == IsSignedType(tb))
		return ta;
	const NumberType signedOne = IsSignedType(ta) ? ta : tb, unsignedOne = IsSignedType(ta) ? tb : ta;
	return r >= 0 ? unsignedOne : signedOne;
}

NumberType CommonFloat(const Value &a, const Value &b) noexcept {
	const bool f64 = a.GetNumberType() == NumberType::F64 || b.GetNumberType() == NumberType::F64;
	return f64 ? NumberType::F64 : NumberType::F32;
}

int Compare(const Value &a, const Value &b) noexcept {
	if (a.IsInteger() && b.IsInteger()) {
		const Wide x = ToWide(a), y = ToWide(b);
		return x < y ? -1 : (x > y ? 1 : 0);
	}
	const double x = a.AsNumber(), y = b.AsNumber();
	if (std::isnan(x) || std::isnan(y))
		return 2;
	return x < y ? -1 : (x > y ? 1 : 0);
}

Result<Value, String> Apply(Op op, const Value &a, const Value &b) {
	if (op == Op::DIVIDE) {
		if (b.AsNumber() == 0.0)
			return Err(String("division par zéro"));
		const NumberType t = a.IsInteger() && b.IsInteger() ? NumberType::F64 : CommonFloat(a, b);
		return Ok(Value::Float(a.AsNumber() / b.AsNumber(), t));
	}
	if (a.IsInteger() && b.IsInteger()) {
		const Wide x = ToWide(a), y = ToWide(b);
		Wide r = 0;
		switch (op) {
			case Op::ADD:
				r = x + y;
				break;
			case Op::SUBTRACT:
				r = x - y;
				break;
			case Op::MULTIPLY: // |x|,|y| < 2^64 : le produit tient sur 128 bits
				if (x != 0 && y != 0 && (x > Wide(UINT64_MAX) || x < -Wide(UINT64_MAX) || y > Wide(UINT64_MAX) ||
										 y < -Wide(UINT64_MAX)))
					return Err(String("dépassement de capacité"));
				r = x * y;
				break;
			default: // MODULO — signe du dividende, comme en C
				if (y == 0)
					return Err(String("modulo par zéro"));
				r = x % y;
				break;
		}
		const NumberType t = CommonInteger(a, b, r);
		if (!Fits(r, t))
			return Err(String::Format("dépassement de capacité : %s ne tient pas dans %s", WideToString(r).CStr(),
									  NumberTypeName(t)));
		return Ok(FromWide(r, t));
	}
	const NumberType t = CommonFloat(a, b);
	const double x = a.AsNumber(), y = b.AsNumber();
	switch (op) {
		case Op::ADD:
			return Ok(Value::Float(x + y, t));
		case Op::SUBTRACT:
			return Ok(Value::Float(x - y, t));
		case Op::MULTIPLY:
			return Ok(Value::Float(x * y, t));
		default:
			if (y == 0.0)
				return Err(String("modulo par zéro"));
			return Ok(Value::Float(std::fmod(x, y), t));
	}
}

Result<Value, String> Negate(const Value &v) {
	if (v.IsFloat())
		return Ok(Value::Float(-v.AsNumber(), v.GetNumberType()));
	const Wide r = -ToWide(v);
	const NumberType t = IsSignedType(v.GetNumberType()) ? v.GetNumberType() : NumberType::I64;
	if (!Fits(r, t))
		return Err(String::Format("dépassement de capacité : %s ne tient pas dans %s", WideToString(r).CStr(),
								  NumberTypeName(t)));
	return Ok(FromWide(r, t));
}

Result<Value, String> ConvertExact(const Value &v, NumberType t) {
	if (!IsIntegerType(t)) {
		if (t == NumberType::F32 && std::isfinite(v.AsNumber()) && std::fabs(v.AsNumber()) > 3.4028234663852886e38)
			return Err(String::Format("%s hors des bornes de f32", v.ToDisplayString().CStr()));
		return Ok(Value::Float(v.AsNumber(), t));
	}
	Wide w = 0;
	if (v.IsInteger())
		w = ToWide(v);
	else {
		const double d = v.AsNumber();
		if (!std::isfinite(d) || d != std::floor(d))
			return Err(String::Format("attendu un entier %s, reçu le nombre %s", NumberTypeName(t),
									  v.ToDisplayString().CStr()));
		if (d < -1.8446744073709552e19 || d > 1.8446744073709552e19)
			return Err(String::Format("%s hors des bornes de %s", v.ToDisplayString().CStr(), NumberTypeName(t)));
		w = static_cast<Wide>(d);
	}
	if (!Fits(w, t))
		return Err(String::Format("%s hors des bornes de %s", v.ToDisplayString().CStr(), NumberTypeName(t)));
	return Ok(FromWide(w, t));
}

Result<Value, String> ConvertWrapping(const Value &v, NumberType t) {
	if (!IsIntegerType(t))
		return Ok(Value::Float(v.AsNumber(), t));
	Wide w = 0;
	if (v.IsInteger())
		w = ToWide(v);
	else {
		const double d = v.AsNumber();
		if (!std::isfinite(d))
			return Err(String::Format("%s n'est pas un nombre fini", v.ToDisplayString().CStr()));
		const double low = BitsOf(t) == 64 ? (IsSignedType(t) ? -9223372036854775808.0 : 0.0) : -1.8446744073709552e19;
		const double high =
			BitsOf(t) == 64 ? (IsSignedType(t) ? 9223372036854775807.0 : 18446744073709551615.0) : 1.8446744073709552e19;
		const double clamped = std::trunc(d) < low ? low : (std::trunc(d) > high ? high : std::trunc(d));
		if (BitsOf(t) == 64)
			return Ok(IsSignedType(t) ? (clamped >= 9223372036854775807.0 ? Value::Int(INT64_MAX)
																		  : Value::Int(static_cast<int64_t>(clamped)))
									  : (clamped >= 18446744073709551615.0 ? Value::UInt(UINT64_MAX)
																		   : Value::UInt(static_cast<uint64_t>(clamped))));
		w = static_cast<Wide>(clamped);
	}
	const int bits = BitsOf(t);
	uint64_t raw = static_cast<uint64_t>(w); // modulo 2^64
	if (bits < 64)
		raw &= (uint64_t(1) << bits) - 1;
	if (!IsSignedType(t))
		return Ok(Value::UInt(raw, t));
	if (bits < 64 && (raw >> (bits - 1)) & 1)
		raw |= ~((uint64_t(1) << bits) - 1); // extension de signe
	return Ok(Value::Int(static_cast<int64_t>(raw), t));
}

Option<Value> Parse(const String &text) {
	const String trimmed = text.Trim();
	const char *p = trimmed.CStr();
	const size_t n = trimmed.GetSize();
	size_t i = (n > 0 && (p[0] == '-' || p[0] == '+')) ? 1 : 0;
	bool integral = i < n;
	Wide w = 0;
	for (size_t k = i; k < n && integral; ++k) {
		if (p[k] < '0' || p[k] > '9' || w > Wide(UINT64_MAX))
			integral = false;
		else
			w = w * 10 + (p[k] - '0');
	}
	if (integral && w <= Wide(UINT64_MAX)) {
		if (p[0] == '-')
			w = -w;
		if (Fits(w, NumberType::I64))
			return Some(Value::Int(static_cast<int64_t>(w)));
		if (Fits(w, NumberType::U64))
			return Some(Value::UInt(static_cast<uint64_t>(w)));
	}
	Option<double> parsed = trimmed.TryParseDouble();
	if (parsed.IsNone())
		return NONE;
	return Some(Value::Number(parsed.Unwrap()));
}

} // namespace numeric

} // namespace data::script
