// Définitions de document/objects.hpp
#include "objects.hpp"

#include <algorithm>
#include <unordered_map>

namespace game_editor::objects {

namespace {

/// Instances non générées de `tree`, dans l'ordre du parcours.
std::vector<scene::NodeId> Instances(const scene::NodeTree& tree) {
	std::vector<scene::NodeId> found;
	tree.Traverse(tree.Root(), [&](scene::NodeId id, const scene::Node& node) {
		if (id != tree.Root() && IsInstance(node) && !IsGenerated(node))
			found.push_back(id);
	});
	return found;
}

/// Copie les enfants de la racine de `from` sous `instance` dans `to`,
/// marqués générés, avec leurs références internes re-câblées (une
/// référence à la racine de l'objet désigne l'instance).
void CopyContent(const scene::NodeTree& from, scene::NodeTree& to, scene::NodeId instance) {
	std::unordered_map<scene::NodeId, scene::NodeId> remap;
	remap.emplace(from.Root(), instance);
	from.Traverse(from.Root(), [&](scene::NodeId id, const scene::Node& node) {
		if (id == from.Root())
			return;
		auto parent = remap.find(node.parent);
		if (parent == remap.end())
			return;
		scene::Node copy = node;
		copy.children.clear();
		copy.Set(String(GENERATED_PROPERTY), scene::PropertyValue::Bool(true));
		remap.emplace(id, to.Add(parent->second, std::move(copy)));
	});
	auto lookup = [&remap](scene::NodeId id) -> Option<scene::NodeId> {
		auto found = remap.find(id);
		if (found == remap.end())
			return NONE;
		return Some(found->second);
	};
	for (const auto& [original, copy] : remap) {
		if (original == from.Root())
			continue;
		scene::Node* node = to.Get(copy);
		for (auto& [key, value] : node->properties.Entries())
			value.RemapNodeRefs(lookup);
		for (scene::Component& component : node->components)
			for (auto& [key, value] : component.props.Entries())
				value.RemapNodeRefs(lookup);
	}
}

/// Retire les enfants générés de `id` (et leurs sous-arbres).
void ClearGenerated(scene::NodeTree& tree, scene::NodeId id) {
	std::vector<scene::NodeId> generated;
	for (scene::NodeId child : tree.ChildrenOf(id))
		if (const scene::Node* node = tree.Get(child); node && IsGenerated(*node))
			generated.push_back(child);
	for (scene::NodeId child : generated)
		(void)tree.Remove(child);
}

void ExpandObject(Project& project, const String& name, std::vector<String>& stack,
				  std::vector<String>& done, ExpandReport& report) {
	if (std::find(done.begin(), done.end(), name) != done.end())
		return;
	SceneDesc* object = project.Fs().FindObject(ObjectRef{name});
	if (!object)
		return;
	if (std::find(stack.begin(), stack.end(), name) != stack.end()) {
		String chain;
		for (const String& s : stack)
			chain += s + String(" → ");
		report.errors.push_back(String::Format("objet « %s » : cycle d'instances (%s%s)",
											   name.CStr(), chain.CStr(), name.CStr()));
		return;
	}
	stack.push_back(name);
	for (const String& dependency : DirectDependencies(object->tree))
		ExpandObject(project, dependency, stack, done, report);
	stack.pop_back();
	ExpandReport own = ExpandInstances(object->tree, project, name);
	report.instances += own.instances;
	report.errors.insert(report.errors.end(), own.errors.begin(), own.errors.end());
	done.push_back(name);
}

} // namespace

bool IsInstance(const scene::Node& node) noexcept {
	return node.HasComponent(String(INSTANCE_COMPONENT));
}

String SourceOf(const scene::Node& node) {
	const scene::PropertyValue* source =
		node.ComponentProperty(String(INSTANCE_COMPONENT), String("source"));
	return source ? source->AsString() : String();
}

bool IsGenerated(const scene::Node& node) noexcept {
	const scene::PropertyValue* flag = node.Get(String(GENERATED_PROPERTY));
	return flag && flag->AsBool(false);
}

void MakeInstance(scene::Node& node, const String& objectName) {
	scene::Component instance;
	instance.type = String(INSTANCE_COMPONENT);
	instance.props.Set(String("source"), scene::PropertyValue::Str(objectName));
	node.SetComponent(std::move(instance));
}

scene::NodeId EditableAncestor(const scene::NodeTree& tree, scene::NodeId id) {
	scene::NodeId current = id;
	while (const scene::Node* node = tree.Get(current)) {
		if (!IsGenerated(*node))
			return current;
		current = node->parent;
	}
	return id;
}

scene::NodeTree StripGenerated(const scene::NodeTree& tree) {
	scene::NodeTree copy = tree;
	std::vector<scene::NodeId> generated;
	copy.Traverse(copy.Root(), [&](scene::NodeId id, const scene::Node& node) {
		// Seules les racines des sous-arbres générés : Remove emporte le reste.
		const scene::Node* parent = copy.Get(node.parent);
		if (IsGenerated(node) && !(parent && IsGenerated(*parent)))
			generated.push_back(id);
	});
	for (scene::NodeId id : generated)
		(void)copy.Remove(id);
	return copy;
}

std::vector<String> DirectDependencies(const scene::NodeTree& tree) {
	std::vector<String> names;
	for (scene::NodeId id : Instances(tree)) {
		String source = SourceOf(*tree.Get(id));
		if (!source.IsEmpty() && std::find(names.begin(), names.end(), source) == names.end())
			names.push_back(source);
	}
	return names;
}

bool DependsOn(const Project& project, const scene::NodeTree& tree, const String& objectName) {
	std::vector<String> pending = DirectDependencies(tree);
	std::vector<String> seen;
	while (!pending.empty()) {
		String name = pending.back();
		pending.pop_back();
		if (name == objectName)
			return true;
		if (std::find(seen.begin(), seen.end(), name) != seen.end())
			continue;
		seen.push_back(name);
		if (const SceneDesc* object = project.Fs().FindObject(ObjectRef{name}))
			for (const String& next : DirectDependencies(object->tree))
				pending.push_back(next);
	}
	return false;
}

ExpandReport ExpandInstances(scene::NodeTree& tree, const Project& project, const String& owner) {
	ExpandReport report;
	for (scene::NodeId id : Instances(tree)) {
		ExpandReport one = ExpandInstance(tree, id, project, owner);
		report.instances += one.instances;
		report.errors.insert(report.errors.end(), one.errors.begin(), one.errors.end());
	}
	return report;
}

ExpandReport ExpandInstance(scene::NodeTree& tree, scene::NodeId id, const Project& project,
							const String& owner) {
	ExpandReport report;
	if (!tree.Contains(id) || !IsInstance(*tree.Get(id)))
		return report;
	ClearGenerated(tree, id);
	const String source = SourceOf(*tree.Get(id));
	const SceneDesc* object = project.Fs().FindObject(ObjectRef{source});
	if (!object) {
		report.errors.push_back(String::Format("%s : objet « %s » introuvable (instance %s)",
											   owner.CStr(), source.CStr(),
											   tree.Get(id)->name.CStr()));
		return report;
	}
	if (source == owner || DependsOn(project, object->tree, owner)) {
		report.errors.push_back(String::Format("%s : l'instance %s de « %s » créerait un cycle",
											   owner.CStr(), tree.Get(id)->name.CStr(),
											   source.CStr()));
		return report;
	}
	CopyContent(object->tree, tree, id);
	++report.instances;
	return report;
}

ExpandReport ExpandProject(Project& project) {
	ExpandReport report;
	std::vector<String> stack;
	std::vector<String> done;
	for (const SceneDesc& document : project.scenes)
		if (document.IsObject())
			ExpandObject(project, document.name, stack, done, report);
	for (SceneDesc& document : project.scenes) {
		if (document.IsObject())
			continue;
		ExpandReport own = ExpandInstances(document.tree, project, document.name);
		report.instances += own.instances;
		report.errors.insert(report.errors.end(), own.errors.begin(), own.errors.end());
	}
	return report;
}

int RenameReferences(Project& project, const String& from, const String& to) {
	int count = 0;
	for (SceneDesc& document : project.scenes) {
		for (scene::NodeId id : Instances(document.tree)) {
			scene::Node* node = document.tree.Get(id);
			if (SourceOf(*node) == from) {
				MakeInstance(*node, to);
				++count;
			}
		}
	}
	return count;
}

} // namespace game_editor::objects
