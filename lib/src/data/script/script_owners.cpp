// Définitions de data/script/script_owners.hpp
#include "data/script/script_owners.hpp"

#include "data/script/script_interpreter.hpp"

#include <algorithm>

namespace data::script {

// ── OwnerObject ──────────────────────────────────────────────────────────────

Option<ScriptError> OwnerObject::OnInit(Interpreter&, std::vector<Value>&) {
	return NONE;
}

void OwnerObject::OnDeinit(Interpreter&) {}

// ── OwnerRegistry ────────────────────────────────────────────────────────────

void OwnerRegistry::Add(const std::shared_ptr<InstanceObject>& instance) {
	if (!instance)
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& item : m_items)
		if (item == instance)
			return;
	m_items.push_back(instance);
}

bool OwnerRegistry::Remove(const InstanceObject* instance) {
	// La référence sort du verrou AVANT d'être relâchée : si c'était la
	// dernière, le destructeur de l'instance (et de ses champs) ne doit pas
	// s'exécuter sous notre verrou.
	std::shared_ptr<InstanceObject> released;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = std::find_if(m_items.begin(), m_items.end(),
							   [instance](const std::shared_ptr<InstanceObject>& item) {
								   return item.get() == instance;
							   });
		if (it == m_items.end())
			return false;
		released = std::move(*it);
		m_items.erase(it);
	}
	return true;
}

std::vector<std::shared_ptr<InstanceObject>> OwnerRegistry::Snapshot() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_items;
}

size_t OwnerRegistry::Count() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_items.size();
}

std::vector<OwnerInfo> OwnerRegistry::Describe() const {
	std::vector<OwnerInfo> out;
	for (const auto& instance : Snapshot()) {
		String scriptName = instance->klass ? instance->klass->Name() : String("?");
		String name;
		if (Option<Value> field = instance->fields.Get(String("name"));
			field.IsSome() && !field.Value().IsNil())
			name = field.Value().ToDisplayString();
		for (const auto& owner : instance->owners) {
			if (!owner || !owner->initialized)
				continue;
			OwnerInfo info;
			info.typeName = owner->type ? owner->type->name : String("?");
			info.scriptName = scriptName;
			info.name = name;
			info.display = DescribeHost(owner.get());
			info.alive = !owner->destroyed;
			out.push_back(std::move(info));
		}
	}
	return out;
}

void OwnerRegistry::DestroyAll(Interpreter& vm) {
	// Par vagues : un `on_destroy` peut créer de nouvelles ressources (un
	// débris, un effet) ; elles sont détruites à la vague suivante. Borné
	// pour qu'un script qui recrée sans fin ne bloque pas l'hôte.
	for (int round = 0; round < 64; ++round) {
		std::vector<std::shared_ptr<InstanceObject>> batch = Snapshot();
		if (batch.empty())
			return;
		for (auto it = batch.rbegin(); it != batch.rend(); ++it)
			vm.DestroyInstance(*it);
		// Une instance déjà marquée détruite mais restée là (destruction
		// interrompue) ne doit pas faire boucler.
		for (const auto& instance : batch)
			(void)Remove(instance.get());
	}
}

// ── Construction et destruction des bases ────────────────────────────────────

void AttachOwners(const std::shared_ptr<InstanceObject>& instance) {
	if (!instance || !instance->klass)
		return;
	for (const auto& type : instance->klass->owners) {
		std::shared_ptr<OwnerObject> owner = type->createOwner ? type->createOwner() : nullptr;
		if (!owner)
			owner = std::make_shared<OwnerObject>();
		owner->type = type;
		owner->self = instance;
		instance->owners.push_back(std::move(owner));
	}
}

Option<ScriptError> InitOwners(Interpreter& vm, const std::shared_ptr<InstanceObject>& instance,
							   const ClassObject& level, std::vector<Value>& args) {
	if (!instance || instance->destroyed)
		return NONE;
	for (const auto& type : level.owners) {
		std::shared_ptr<OwnerObject> owner = FindOwner(*instance, *type);
		if (!owner || owner->initialized || owner->destroyed)
			continue;
		if (type->mainThreadOnly && !vm.IsMainThread())
			return Some(Interpreter::MakeError(
				String::Format("`%s` : une base de l'hôte se construit sur le fil principal, pas "
							   "depuis une fonction `async`",
							   type->name.CStr())));
		// Chaque base reçoit SA copie : l'une ne voit pas ce qu'une autre a
		// consommé ou modifié.
		std::vector<Value> own = args;
		if (auto error = owner->OnInit(vm, own); error.IsSome())
			return error;
		owner->initialized = true;
		vm.Owners().Add(instance);
	}
	return NONE;
}

void DeinitOwners(Interpreter& vm, const std::shared_ptr<InstanceObject>& instance) {
	if (!instance)
		return;
	// Ordre INVERSE de la construction, comme les destructeurs C++ : une
	// base déclarée après une autre peut encore s'appuyer sur elle pendant
	// sa propre libération.
	for (auto it = instance->owners.rbegin(); it != instance->owners.rend(); ++it) {
		const std::shared_ptr<OwnerObject>& owner = *it;
		if (!owner || owner->destroyed)
			continue;
		owner->destroyed = true;
		if (owner->initialized)
			owner->OnDeinit(vm);
	}
	(void)vm.Owners().Remove(instance.get());
}

bool IsOwned(const InstanceObject& instance) noexcept {
	return instance.klass && !instance.klass->owners.empty();
}

std::shared_ptr<OwnerObject> FindOwner(const InstanceObject& instance,
									   const HostType& type) noexcept {
	for (const auto& owner : instance.owners)
		if (owner && owner->type.get() == &type)
			return owner;
	return nullptr;
}

std::shared_ptr<OwnerObject> OwnerWithMethod(const InstanceObject& instance, const String& name) {
	for (const auto& owner : instance.owners)
		if (owner && owner->type && owner->type->FindMethod(name))
			return owner;
	return nullptr;
}

Option<Value> ReadOwnerProperty(const InstanceObject& instance, const String& name) {
	for (const auto& owner : instance.owners) {
		if (!owner || !owner->type || !owner->type->get)
			continue;
		if (Option<Value> value = owner->type->get(*owner, name); value.IsSome())
			return value;
	}
	return NONE;
}

} // namespace data::script
