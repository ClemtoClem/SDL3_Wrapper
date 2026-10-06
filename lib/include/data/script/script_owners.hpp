#pragma once
/**
 * data::script — « owners » : des types de base fournis par l'HÔTE (C++) dont
 * les classes de script DÉRIVENT, avec un cycle de vie RAII explicite.
 *
 * L'hôte (un moteur de jeu, un éditeur) définit en C++ ses classes de base —
 * une ressource de scène, un corps physique, un abonnement aux évènements —
 * et les enregistre comme `HostType` marqués `isOwner`. Un script en hérite,
 * seules ou à côté d'UNE classe parente de script :
 *
 *     class Ennemi extends Mesh3D, PhysicsBody, Gameplay {
 *         fn init(nom) {
 *             super.init(nom)                      # parent de script, puis owners
 *             this.set_mesh("suzanne.glb")         # méthode de Mesh3D
 *             this.set_body({kind: "dynamic", mass: 80})   # PhysicsBody
 *             this.on("hit", fn(dégâts) { … })     # Gameplay
 *         }
 *         fn on_destroy() { print("adieu " .. this.name) }
 *         fn deinit()     { … }
 *     }
 *
 *     let e = Ennemi("gobelin")   # → construite, enregistrée, active
 *     e.destroy()                 # → séquence RAII ci-dessous
 *
 * ── Construction ────────────────────────────────────────────────────────
 *
 * `Ennemi(…)` crée l'instance et UN `OwnerObject` par base de l'hôte (celles
 * de la classe et de ses ancêtres), encore NON initialisés, puis appelle
 * `init`. `super.init(args)` initialise le parent de script puis les bases de
 * l'hôte, dans l'ordre de déclaration (`OwnerObject::OnInit(args)`). Une base
 * que `init` n'a pas initialisée (pas de `super.init`) l'est à la sortie de
 * `init`, sans argument — comme un constructeur par défaut C++. Appeler une
 * méthode d'une base pas encore initialisée est une erreur explicite.
 *
 * Dès qu'une base est initialisée, l'instance est ENREGISTRÉE dans le
 * registre de son interpréteur (`Interpreter::Owners()`), qui la garde en
 * vie : une ressource de l'éditeur ne disparaît pas parce que le script a
 * oublié sa variable — elle vit jusqu'à `destroy()` ou la fin de la scène.
 *
 * ── Destruction (explicite, idempotente) ────────────────────────────────
 *
 *     instance.destroy()                     # ou registre.DestroyAll()
 *       ├─ on_destroy()                      # le script libère ses paires
 *       ├─ deinit() de chaque classe         # dérivée d'abord (cf. RunDeinit)
 *       └─ pour chaque base de l'hôte, en ordre INVERSE :
 *             OwnerObject::OnDeinit()        # C++ : nœud, corps, abonnements
 *       └─ retrait du registre               # l'instance redevient ordinaire
 *
 * Le second appel (à la main, puis en fin de scène) ne fait rien. Après la
 * destruction, l'objet de script existe encore (ses champs restent lisibles)
 * mais ses méthodes de base de l'hôte rendent une erreur « détruite ».
 *
 * ── Pourquoi un registre côté C++ ───────────────────────────────────────
 *
 * Un script bogué ne doit pas bloquer l'hôte : l'outliner, la fin de partie
 * et la destruction de l'interpréteur parcourent et détruisent les
 * ressources SANS passer par le script (`OwnerRegistry::Describe`,
 * `DestroyAll`). Le registre est par interpréteur : ce qu'un interpréteur a
 * produit part avec lui (cf. Interpreter::~Interpreter).
 */
#include "data/script/script_value.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace data::script {

/// La face C++ d'une base de l'hôte, une par (instance de script × base).
/// L'hôte en DÉRIVE pour y ranger l'état de sa ressource, et redéfinit
/// `OnInit` / `OnDeinit` : ce sont le constructeur et le destructeur C++ de
/// la base. Dérive de `HostObject` : elle porte son `HostType` (méthodes,
/// propriétés, affichage).
struct OwnerObject : HostObject {
	/// L'instance de script qui la possède — FAIBLE : c'est l'instance qui la
	/// tient (InstanceObject::owners), pas de cycle.
	std::weak_ptr<InstanceObject> self;
	/// `OnInit` a réussi : les méthodes de la base sont utilisables.
	bool initialized = false;
	/// `OnDeinit` est passé : plus aucune méthode de la base n'est permise.
	bool destroyed = false;

	/// `super.init(args)` (ou, à défaut, sortie de `init` sans argument).
	/// Les arguments sont ceux de `super.init` : chaque base y prend ce qui la
	/// concerne. Une erreur annule la construction de l'instance.
	virtual Option<ScriptError> OnInit(Interpreter& vm, std::vector<Value>& args);
	/// Libère la ressource C++. Appelé une fois, en ordre inverse des bases.
	virtual void OnDeinit(Interpreter& vm);

	[[nodiscard]] std::shared_ptr<InstanceObject> Self() const { return self.lock(); }

	/// Une autre base de la même instance (`Sibling<Mesh3DOwner>()`), ou
	/// nullptr. Permet à une base de s'appuyer sur une autre (le corps
	/// physique sur le nœud créé par la base « maillage »).
	template <class T> [[nodiscard]] std::shared_ptr<T> Sibling() const;
};

/// Ce que l'hôte montre d'une ressource (outliner, console, rapport).
struct OwnerInfo {
	String typeName;   ///< nom qualifié de la base (`game.Mesh3D`)
	String scriptName; ///< classe de script qui en dérive (`Ennemi`)
	String name;	   ///< champ `name` de l'instance, s'il existe
	String display;	   ///< DescribeHost de l'owner
	bool alive = true;
};

/// Instances de script qui possèdent au moins une base initialisée, dans
/// l'ordre de leur enregistrement. Tient des références FORTES : c'est ce
/// qui garde la ressource en vie jusqu'à `destroy()`.
class OwnerRegistry {
public:
	void Add(const std::shared_ptr<InstanceObject>& instance);
	/// Retire l'instance ; rend vrai si elle y était.
	bool Remove(const InstanceObject* instance);
	[[nodiscard]] std::vector<std::shared_ptr<InstanceObject>> Snapshot() const;
	[[nodiscard]] size_t Count() const;
	/// Une entrée par owner vivant.
	[[nodiscard]] std::vector<OwnerInfo> Describe() const;
	/// Détruit tout, du plus récent au plus ancien (séquence RAII complète).
	/// Une instance créée PENDANT la destruction (un `on_destroy` qui
	/// fabrique un débris) est détruite elle aussi.
	void DestroyAll(Interpreter& vm);

private:
	mutable std::mutex m_mutex;
	std::vector<std::shared_ptr<InstanceObject>> m_items;
};

/// Crée les `OwnerObject` (non initialisés) d'une instance neuve, un par base
/// de l'hôte de sa classe et de ses ancêtres. Appelé par Instantiate.
void AttachOwners(const std::shared_ptr<InstanceObject>& instance);

/// Initialise les bases de l'hôte déclarées par `level` ou ses ancêtres qui
/// ne le sont pas encore (`super.init(args)`), et enregistre l'instance.
[[nodiscard]] Option<ScriptError> InitOwners(Interpreter& vm,
											 const std::shared_ptr<InstanceObject>& instance,
											 const ClassObject& level, std::vector<Value>& args);

/// Libère les bases initialisées en ordre inverse et retire l'instance du
/// registre. Idempotent.
void DeinitOwners(Interpreter& vm, const std::shared_ptr<InstanceObject>& instance);

/// La classe de l'instance dérive d'au moins une base de l'hôte.
[[nodiscard]] bool IsOwned(const InstanceObject& instance) noexcept;

/// L'owner d'une instance pour le type donné (exact), ou nullptr.
[[nodiscard]] std::shared_ptr<OwnerObject> FindOwner(const InstanceObject& instance,
													 const HostType& type) noexcept;

/// La première base de l'instance (ordre de `owners`) qui fournit la
/// méthode `name`, ou nullptr. Initialisée ou non : c'est la méthode qui
/// refuse un appel prématuré ou tardif, avec un message explicite.
[[nodiscard]] std::shared_ptr<OwnerObject> OwnerWithMethod(const InstanceObject& instance,
														   const String& name);

/// La première base dont le crochet `get` connaît la propriété `name`.
[[nodiscard]] Option<Value> ReadOwnerProperty(const InstanceObject& instance, const String& name);

/**
 * Construit le `HostType` d'une base de l'hôte dont l'état est `T` (dérivé de
 * `OwnerObject`). Les méthodes reçoivent directement `T&`, déjà vérifié
 * initialisé et non détruit :
 *
 *     OwnerTypeBuilder<Mesh3DOwner> mesh;
 *     mesh.Method("set_mesh", 1, 1, [](Interpreter& vm, Mesh3DOwner& self, std::vector<Value>&
 * args) { … }); vm.RegisterHostType("game", "Mesh3D", mesh.Type());
 */
template <class T> class OwnerTypeBuilder {
public:
	using MethodFn =
		std::function<Result<Value, ScriptError>(Interpreter&, T&, std::vector<Value>&)>;
	using GetterFn = std::function<Option<Value>(const T&)>;

	/// Le nom qualifié (`game.Mesh3D`) est donné par RegisterHostType ;
	/// `factory` fabrique l'état d'une nouvelle instance (défaut : `T()`).
	explicit OwnerTypeBuilder(std::function<std::shared_ptr<T>()> factory = nullptr)
		: m_type(std::make_shared<HostType>()) {
		m_type->isOwner = true;
		m_type->mainThreadOnly = true; // l'état d'un hôte n'est pas partagé entre fils
		if (!factory)
			factory = [] { return std::make_shared<T>(); };
		m_type->createOwner = [factory]() -> std::shared_ptr<OwnerObject> { return factory(); };
	}

	OwnerTypeBuilder& Method(const char* name, int minArity, int maxArity, MethodFn fn) {
		HostMethod method;
		method.name = String(name);
		method.minArity = minArity;
		method.maxArity = maxArity;
		const HostType* type =
			m_type.get(); // la méthode vit dans son type : pointeur toujours valide
		method.fn = [fn = std::move(fn), type,
					 name = method.name](Interpreter& vm, const HostRef& self,
										 std::vector<Value>& args) -> Result<Value, ScriptError> {
			const String& qualified = type->name;
			auto owner = std::dynamic_pointer_cast<T>(self);
			if (!owner)
				return Err(ScriptError(
					String::Format("`%s.%s` : objet incompatible", qualified.CStr(), name.CStr()),
					0, 0));
			if (owner->destroyed)
				return Err(
					ScriptError(String::Format("`%s.%s` : la ressource a été détruite (destroy)",
											   qualified.CStr(), name.CStr()),
								0, 0));
			if (!owner->initialized)
				return Err(
					ScriptError(String::Format("`%s.%s` : base pas encore initialisée — appelez "
											   "`super.init(…)` avant d'utiliser ses méthodes",
											   qualified.CStr(), name.CStr()),
								0, 0));
			return fn(vm, *owner, args);
		};
		m_type->methods.push_back(std::move(method));
		return *this;
	}

	/// Propriété en lecture (`this.position`) : NONE = inconnue.
	OwnerTypeBuilder& Getter(GetterFn fn) {
		m_type->get = [fn = std::move(fn)](const HostObject& object,
										   const String&) -> Option<Value> {
			const T* owner = dynamic_cast<const T*>(&object);
			if (!owner || owner->destroyed || !owner->initialized)
				return NONE;
			return fn(*owner);
		};
		return *this;
	}

	/// Propriétés nommées en lecture (plus pratique que Getter quand elles
	/// sont peu nombreuses).
	OwnerTypeBuilder& Property(const char* name, std::function<Value(const T&)> fn) {
		m_properties.emplace_back(String(name), std::move(fn));
		auto properties = m_properties;
		m_type->get = [properties](const HostObject& object, const String& key) -> Option<Value> {
			const T* owner = dynamic_cast<const T*>(&object);
			if (!owner || owner->destroyed || !owner->initialized)
				return NONE;
			for (const auto& [propName, read] : properties)
				if (propName == key)
					return Some(read(*owner));
			return NONE;
		};
		return *this;
	}

	OwnerTypeBuilder& Display(std::function<String(const T&)> fn) {
		m_type->display = [fn = std::move(fn)](const HostObject& object) -> String {
			const T* owner = dynamic_cast<const T*>(&object);
			return owner ? fn(*owner) : String("?");
		};
		return *this;
	}

	[[nodiscard]] const std::shared_ptr<HostType>& Type() const noexcept { return m_type; }

private:
	std::shared_ptr<HostType> m_type;
	std::vector<std::pair<String, std::function<Value(const T&)>>> m_properties;
};

template <class T> std::shared_ptr<T> OwnerObject::Sibling() const {
	auto instance = self.lock();
	if (!instance)
		return nullptr;
	for (const auto& other : instance->owners)
		if (auto typed = std::dynamic_pointer_cast<T>(other))
			return typed;
	return nullptr;
}

} // namespace data::script
