#pragma once
/**
 * data::script — l'ECS de la bibliothèque (ecs::ArchetypeRegistry) vu depuis
 * le langage (« Script »), dans l'espace de noms `ecs`.
 *
 * Un composant C++ est un TYPE, qu'un script ne peut pas déclarer. Le script
 * manipule donc deux sortes de composants, sous des NOMS :
 *   - ses propres composants — une valeur quelconque (nombre, table, objet…)
 *     rangée dans le composant C++ `ScriptComponents` de l'entité ;
 *   - les composants C++ que l'hôte a déclarés (EcsWorld::BindComponent),
 *     convertis à chaque lecture/écriture (`transform`, `name`…).
 *
 *   let monde = ecs.world()                      # un monde propre au script
 *   let e = monde.spawn({pos: [0, 0], vit: [1, 0]})
 *   monde.system("mouvement", ["pos", "vit"], fn(e, pos, vit, dt) {
 *       pos[0] += vit[0] * dt                    # les tables/listes sont des références
 *   })
 *   monde.run(0.016)                             # exécute les systèmes, dans l'ordre
 *   for (e in monde.query("pos")) { print(e.get("pos")) }
 *   let scène = ecs.host()                       # le monde de l'hôte (s'il l'expose)
 *
 * Les requêtes MATÉRIALISENT la liste des entités avant d'appeler le script :
 * un système peut créer ou détruire des entités sans invalider l'itération
 * (le piège documenté en tête de ecs.hpp).
 *
 * Fils : un monde créé par le script a son verrou et se partage entre fils
 * `async` ; le monde de l'HÔTE n'est manipulé que sur le fil principal (ses
 * méthodes y sont renvoyées, cf. HostType::mainThreadOnly).
 */
#include <memory>
#include <mutex>
#include <vector>

#include "../../ecs/ecs.hpp"
#include "script_std.hpp"

namespace data::script {

/// Composants d'une entité posés par le script : nom → valeur.
struct ScriptComponents {
	std::vector<std::pair<String, Value>> items;

	[[nodiscard]] Value *Find(const String &name) noexcept;
};

/// Ressources (singletons) posées par le script dans un monde.
struct ScriptResources {
	std::vector<std::pair<String, Value>> items;
};

/**
 * Un monde ECS exposé au script : un registre (possédé, ou celui de l'hôte),
 * les composants C++ que l'hôte y déclare, et les systèmes du script.
 */
class EcsWorld {
public:
	/// Composant C++ de l'hôte, vu sous un nom. `set` nul : lecture seule.
	struct HostComponent {
		String name;
		std::function<bool(ecs::ArchetypeRegistry &, ecs::Entity)> has;
		std::function<Value(ecs::ArchetypeRegistry &, ecs::Entity)> get;
		std::function<Option<String>(ecs::ArchetypeRegistry &, ecs::Entity, const Value &)> set;
		std::function<bool(ecs::ArchetypeRegistry &, ecs::Entity)> remove;
		std::function<std::vector<ecs::Entity>(ecs::ArchetypeRegistry &)> entities;
	};

	struct System {
		String name;
		std::vector<String> components;
		Value fn;
	};

	/// Monde possédé (créé par le script).
	EcsWorld() : m_owned(std::make_unique<ecs::ArchetypeRegistry>()), m_registry(m_owned.get()) {}
	/// Monde de l'hôte : NON possédé, doit survivre aux scripts qui l'utilisent.
	explicit EcsWorld(ecs::ArchetypeRegistry &registry) : m_registry(&registry) {}

	EcsWorld(const EcsWorld &) = delete;
	EcsWorld &operator=(const EcsWorld &) = delete;

	[[nodiscard]] bool IsOwned() const noexcept { return m_owned != nullptr; }
	[[nodiscard]] ecs::ArchetypeRegistry &Registry() noexcept { return *m_registry; }
	[[nodiscard]] std::mutex &Mutex() noexcept { return m_mutex; }

	/**
	 * Expose le composant C++ `T` sous `name`. `toValue` le convertit en
	 * valeur du script ; `fromValue` (facultatif : sinon lecture seule)
	 * remplit un `T` depuis une valeur — ou rend la raison du refus.
	 */
	template <typename T>
	void BindComponent(const String &name, std::function<Value(const T &)> toValue,
					   std::function<Option<String>(const Value &, T &)> fromValue = nullptr) {
		HostComponent component;
		component.name = name;
		component.has = [](ecs::ArchetypeRegistry &r, ecs::Entity e) { return r.HasComponent<T>(e); };
		component.get = [toValue](ecs::ArchetypeRegistry &r, ecs::Entity e) -> Value {
			auto found = r.GetComponent<T>(e);
			return found.IsSome() ? toValue(*found.Unwrap()) : Value::Nil();
		};
		if (fromValue) {
			component.set = [fromValue](ecs::ArchetypeRegistry &r, ecs::Entity e, const Value &v) -> Option<String> {
				T value{};
				if (auto existing = r.GetComponent<T>(e); existing.IsSome())
					value = *existing.Unwrap();
				if (Option<String> bad = fromValue(v, value); bad.IsSome())
					return bad;
				if (auto existing = r.GetComponent<T>(e); existing.IsSome())
					*existing.Unwrap() = std::move(value);
				else
					r.AddComponent(e, std::move(value));
				return NONE;
			};
			component.remove = [](ecs::ArchetypeRegistry &r, ecs::Entity e) { return r.RemoveComponent<T>(e); };
		}
		component.entities = [](ecs::ArchetypeRegistry &r) { return r.EntitiesWith<T>(); };
		std::lock_guard<std::mutex> lock(m_mutex);
		m_hostComponents.push_back(std::move(component));
	}

	[[nodiscard]] const HostComponent *FindHost(const String &name) const noexcept;
	[[nodiscard]] const std::vector<HostComponent> &HostComponents() const noexcept { return m_hostComponents; }
	std::vector<System> &Systems() noexcept { return m_systems; }

private:
	std::unique_ptr<ecs::ArchetypeRegistry> m_owned;
	ecs::ArchetypeRegistry *m_registry = nullptr;
	std::mutex m_mutex;
	std::vector<HostComponent> m_hostComponents;
	std::vector<System> m_systems;
};

namespace ecslib {

using lib::Args;
using lib::As;
using lib::Fail;
using lib::TypeBuilder;

struct WorldObject : HostObject {
	std::shared_ptr<EcsWorld> world;
};

struct EntityObject : HostObject {
	std::shared_ptr<EcsWorld> world;
	ecs::Entity entity{};
};

/// Entité sous forme d'un entier (index + génération << 32) : ce que rend
/// `e.id`, et ce que relit `monde.entity(id)`.
[[nodiscard]] inline uint64_t PackEntity(ecs::Entity e) { return uint64_t(e.id) | (uint64_t(e.generation) << 32); }
[[nodiscard]] ecs::Entity UnpackEntity(uint64_t id);

/// Types de CET interpréteur (pour fabriquer les entités).
[[nodiscard]] std::shared_ptr<const HostType> EntityType(Interpreter &vm);

[[nodiscard]] Value MakeEntity(Interpreter &vm, const std::shared_ptr<EcsWorld> &world, ecs::Entity entity);

// ── Opérations sur un monde (verrou tenu par l'appelant) ────────────────────

[[nodiscard]] bool HasComponent(EcsWorld &world, ecs::Entity e, const String &name);

[[nodiscard]] Value GetComponent(EcsWorld &world, ecs::Entity e, const String &name);

[[nodiscard]] Option<ScriptError> SetComponent(EcsWorld &world, ecs::Entity e, const String &name, Value value);

[[nodiscard]] bool RemoveComponent(EcsWorld &world, ecs::Entity e, const String &name);

[[nodiscard]] std::vector<String> ComponentNames(EcsWorld &world, ecs::Entity e);

/// Toutes les entités VUES par le script : celles qui portent un composant
/// du script ou un composant déclaré par l'hôte (sans doublon, triées).
[[nodiscard]] std::vector<ecs::Entity> AllEntities(EcsWorld &world);

/// Entités qui portent TOUS les composants nommés.
[[nodiscard]] std::vector<ecs::Entity> Query(EcsWorld &world, const std::vector<String> &names);

// ── Arguments ───────────────────────────────────────────────────────────────

[[nodiscard]] Result<ecs::Entity, ScriptError> ArgEntity(const Args &args, size_t i, const EcsWorld &world,
																const char *fn);

/// Noms de composants : `"pos"`, `"pos", "vit"` ou `["pos", "vit"]`.
[[nodiscard]] Result<std::vector<String>, ScriptError> ArgNames(const Args &args, size_t from, size_t to,
																	   const char *fn);

/// Appelle `fn(e, composants…[, extra])` pour chaque entité de la requête
/// (matérialisée d'abord) ; rend le nombre d'appels.
[[nodiscard]] Result<int64_t, ScriptError> RunQuery(Interpreter &vm, const std::shared_ptr<EcsWorld> &world,
														   const std::vector<String> &names, const Value &fn,
														   const Option<Value> &extra);

void DefineWorld(TypeBuilder &builder, bool owned);

void DefineEntity(TypeBuilder &builder);

} // namespace ecslib

/**
 * Installe l'espace de noms `ecs` : `ecs.world()` (un monde propre au
 * script) et, si `hostWorld` est donné, `ecs.host()` — le monde de l'hôte,
 * avec les composants qu'il y a déclarés (EcsWorld::BindComponent).
 */
void InstallEcsLibrary(Interpreter &vm, std::shared_ptr<EcsWorld> hostWorld = nullptr);

} // namespace data::script
