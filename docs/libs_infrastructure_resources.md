# resources — ressources, cache, chargement asynchrone et catalogue d'assets

`libs/infrastructure/resources` · couche **infrastructure** · `libresources.a` · espace de noms `resources::`

## Présentation

`resources` gère le cycle de vie des données chargées depuis le disque, ou
générées : maillages, textures, matériaux, shaders, sons, contenus bruts…

- **`Resource`** est la base abstraite. Elle a un état atomique (`Unloaded`,
  `Loading`, `Ready`, `Failed`) et des fonctions de rappel « prête ».
- **`AsyncLoader`** est un vivier de fils (`sdl3::Thread`) qui charge en
  arrière-plan, avec deux priorités.
- **`ResourceCache`** associe un registre typé et le chargeur. On y désigne
  une ressource de deux façons :
  - **par chemin**, pour un cache simple ;
  - **par identifiant stable** (`AssetId` / `AssetRef<T>`). C'est ce que les
    composants ECS et les fichiers écrivent. Le cache tient alors un
    **catalogue** (identifiant → chemin ou clé et paramètres) et recrée la
    ressource à la demande grâce à une **fabrique** par type.
- **Références manquantes** : une référence qui ne se résout pas est recensée
  (`Missing()`) au lieu de casser le chargement d'un fichier.

Les types concrets sont définis par les modules qui les produisent :

- [`render3d`](libs_infrastructure_render3d.md) : maillages, textures,
  matériaux et shaders (`render3d::RegisterResourceFactories`) ;
- [`audio`](libs_infrastructure_audio.md) : clips audio.

`resources` ne contient que `BytesResource` (contenu brut d'un fichier).

## Architecture interne

| Fichier | Contenu |
|---|---|
| `resources.hpp` | En-tête parapluie |
| `resource.hpp` | `ResourceState`, `Resource` |
| `asset.hpp`, `src/asset.cpp` | `AssetType`, `AssetId`, `AssetTypeOf<T>` (trait), `AssetRef<T>`, `AssetRecord`, `MissingAsset`, `AssetCatalog` |
| `pool.hpp`, `src/pool.cpp` | `Pool<T>` (table nommée et thread-safe), `Registry` (un `Pool` par type) |
| `cache.hpp`, `src/cache.cpp` | `AsyncLoader`, `LoadPolicy`, `ResourceCache`, `CacheRef` |
| `bytes_resource.hpp`, `src/bytes_resource.cpp` | `BytesResource` |

Dépendances : `core`, `sdl3` (fils, mutex, lecture de fichiers),
`dataio` (catalogue en JSON, paramètres des enregistrements).

## API publique

### `Resource`

| Membre | Rôle |
|---|---|
| `GetState()`, `IsReady()`, `IsLoading()`, `HasFailed()`, `IsUnloaded()` | État (lecture atomique, sûre entre fils) |
| `virtual StringView Path() const = 0` | Clé de la ressource |
| `Id()`, `Type()`, `DisplayName()` | Identité dans le catalogue |
| `Result<bool, String> Load()` | Appelle `DoLoad` si la ressource est `Unloaded` ou `Failed`. Déjà prête : `Ok(true)`. En cours ailleurs : `Ok(false)`. Un seul `DoLoad` à la fois |
| `Unload()`, `OnReady(callback)`, `LastError()` | Libération, rappel, message d'échec |
| `virtual Result<bool, String> DoLoad() = 0`, `virtual void DoUnload() = 0` | À implémenter |

`DoLoad` peut s'exécuter **sur un fil de chargement**. Son message d'erreur
doit être une `String` possédée : copiez `sdl3::GetError()` avant de rendre
la main.

### `AsyncLoader`

`AsyncLoader(nombreDeFils)` :

- `Enqueue(res, Priority::NORMAL | HIGH)` rend un `std::future<bool>` ;
- `EnqueueAll(span)` met plusieurs ressources en file ;
- `PendingCount()`, `IsIdle()` et `WaitIdle(délai)` suivent la file ;
- `static LoadSync(res)` charge sur place.

### `ResourceCache`

**Par chemin**

- `LoadAsync(ptr)` et `EmplaceAsync<T>(args…)` chargent en arrière-plan et
  rendent un `std::future<bool>`.
- `LoadSync(ptr)` et `EmplaceSync<T>(args…)` chargent sur place.
- `Get<T>(clé)` rend `Option<shared_ptr<T>>`. `Contains`, `Remove`,
  `Clear<T>` et `ClearAll` gèrent le registre.
- `Resolve(relatif)` préfixe la racine (`basePath`).

**Par identifiant**

| Membre | Rôle |
|---|---|
| `RegisterFactory(AssetType, Factory)` / `HasFactory` | Fabrique qui crée la ressource (non chargée) depuis son `AssetRecord` |
| `AssetId Add(res, record, LoadPolicy)` | Catalogue et garde une ressource déjà construite |
| `AssetId Declare(record)` | Catalogue seulement : création au premier `Get` |
| `Get(AssetRef<T>, contexte)` | La ressource dans son état actuel, recréée et mise en chargement si besoin (`nullptr` si l'id est inconnu ou d'un autre type) |
| `GetReady(AssetRef<T>, contexte)` | La ressource si elle est prête, sinon `nullptr` : l'appel à faire à chaque image |
| `Find(AssetId)`, `LoadNow(id)`, `Knows`, `IsResident`, `Record`, `RecordByPath`, `RecordByKey`, `UpdateRecord` | Accès sans type statique (éditeurs, sérialiseurs) |
| `Missing()` / `ClearMissing()` | Références non résolues et ressources en échec |
| `ResidentCount(type)`, `UnloadUnused()` | Mémoire : libère ce que plus personne ne tient (le catalogue en garde la trace) |
| `CatalogToJson()`, `LoadCatalog(json)`, `CatalogSnapshot()` | Persistance du catalogue (`{"format": "assets.catalog", "version": 1, …}`) |

`CacheRef {ResourceCache*}` est une poignée non propriétaire à ranger dans
les ressources d'un monde ECS, pour que les systèmes résolvent leurs
`AssetRef`.

### Identifiants

- **`AssetId`** :
  - `FromPath(chemin)` est déterministe : réimporter le même fichier redonne
    le même identifiant ;
  - `FromKey(clé)` donne l'identifiant d'une ressource générée, par exemple
    `"box 2 4.6 0.6"` ;
  - `Generate()`, `Parse()` et `ToString()` complètent la classe.
- **`AssetRef<T>`** est une référence **typée** : elle ne compile qu'avec le
  bon type. Elle s'écrit dans un fichier par `ToString()` et se relit par
  `FromString()`. Chaque type concret spécialise
  `AssetTypeOf<T>::value`.
- **`AssetCatalog`** :
  - `Put` (ajoute ou remplace), `Remove` ;
  - `Find`, `FindByPath`, `FindByKey`, `OfType` ;
  - `ToJson()` et `FromJson()` pour la persistance.

## Exemple

```cpp
#include <cstdio>
#include <resources/resources.hpp>

// Une ressource personnalisée : le texte d'un fichier, découpé en lignes.
class TextResource : public resources::Resource {
public:
	explicit TextResource(String path) : m_path(std::move(path)) {}
	[[nodiscard]] StringView Path() const noexcept override { return m_path.View(); }
	[[nodiscard]] const std::vector<String> &Lines() const noexcept { return m_lines; }

protected:
	Result<bool, String> DoLoad() override { // peut tourner sur un fil de chargement
		Option<String> text = String::ReadFile(m_path);
		if (text.IsNone())
			return Err(String("illisible : ") + m_path);
		m_lines = text.Unwrap().Lines();
		return Ok(true);
	}
	void DoUnload() override { m_lines.clear(); }

private:
	String m_path;
	std::vector<String> m_lines;
};

int main() {
	resources::ResourceCache cache("", 2); // 2 fils de chargement

	// 1. Par chemin, en arrière-plan
	std::future<bool> done = cache.EmplaceAsync<TextResource>("cpp-maker.toml");
	std::printf("chargé : %d\n", done.get());
	if (auto text = cache.Get<TextResource>("cpp-maker.toml"); text.IsSome())
		std::printf("%zu lignes\n", text.Unwrap()->Lines().size());

	// 2. Par identifiant stable : catalogue + fabrique
	cache.RegisterFactory(resources::AssetType::BYTES, [](const resources::AssetRecord &rec, const resources::ResourceCache &c) {
		return std::make_shared<resources::BytesResource>(c.Resolve(rec.path.View()));
	});
	resources::AssetRecord record;
	record.id = resources::AssetId::FromPath("README.md");
	record.type = resources::AssetType::BYTES;
	record.name = "Lisez-moi";
	record.path = "README.md";
	resources::AssetId id = cache.Declare(record);

	resources::AssetRef<resources::BytesResource> ref{id}; // ce qu'un composant ou un fichier conserve
	cache.LoadNow(id);
	if (auto bytes = cache.GetReady(ref))
		std::printf("%s : %zu octets\n", bytes->DisplayName().CStr(), bytes->Bytes().size());

	resources::AssetRef<resources::BytesResource> broken{resources::AssetId::Generate()};
	(void)cache.Get(broken, "/scenes/Démo");
	for (const resources::MissingAsset &m : cache.Missing())
		std::printf("manquant (%s) : %s\n", m.context.CStr(), m.reason.CStr());
	return 0;
}
```

Sortie :

```
chargé : 1
172 lignes
Lisez-moi : … octets
manquant (/scenes/Démo) : référence inconnue du catalogue
```

## Intégration

`modules = ["resources"]`.

- Créez **un** `ResourceCache` par application. Il n'est ni copiable ni
  déplaçable, car il possède ses fils.
- Avec `threadCount = 0` (valeur par défaut), le chargeur ouvre autant de
  fils que de cœurs logiques (`hardware_concurrency`).
- Les modules qui fournissent des types enregistrent leurs fabriques au
  démarrage : appelez par exemple `render3d::RegisterResourceFactories(cache)`.

## Tests

```sh
cpp-maker test -m resources
```

Le module [tests/integration](tests_integration.md) vérifie aussi le cache
avec les fabriques de `render3d` et de `audio`.
