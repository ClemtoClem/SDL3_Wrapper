# script_bindings — les modules du dépôt vus depuis Script

`libs/infrastructure/script_bindings` · couche **infrastructure** · `libscript_bindings.a` · espace de noms `script::`

## Présentation

`script_bindings` expose aux scripts du langage
[Script](libs_infrastructure_script.md) les modules du dépôt. Chaque module
correspond à un espace de noms du langage et à une fonction `Install*Library`
autonome :

| Espace (script) | Fonction C++ | Dépendance fournie par l'hôte |
|---|---|---|
| `archive` | `InstallArchiveLibrary(vm)` | aucune (fichiers du disque) |
| `audio` | `InstallAudioLibrary(vm, engine, resolve)` | `audio::AudioEngine` |
| `dataio` | `InstallDataioLibrary(vm)` | aucune |
| `ecs` | `InstallEcsLibrary(vm, hostWorld)` | `EcsWorld` (monde de l'hôte, facultatif) |
| `gen` | `InstallGeneratorLibrary(vm)` | aucune |
| `physics` | `InstallPhysicsLibrary(vm, world)` | `physics::World` |
| `render3d` | `InstallRender3DLibrary(vm, canvas, root)` | `render3d::Canvas` et racine `Object3D` |
| `sql` | `InstallSqlLibrary(vm, hostDatabase)` | `sql::Database` (facultative) |
| `ui` | `InstallUiLibrary(vm, binding)` | `UiBinding` |

`InstallAll(vm, BindingsConfig)` installe d'un coup toutes les bibliothèques
dont les dépendances sont présentes dans la configuration. Il rend la liste des
espaces installés ; les autres sont ignorés sans erreur.

Le module est utilisé par la [sandbox](demo_sandbox.md). Le moteur
[voxel](libs_infrastructure_voxel.md) n'en dépend pas : il expose ses propres
fonctions aux mods directement par `script`.

## Architecture interne

| Fichier | Contenu |
|---|---|
| `script_bindings.hpp`, `src/script_bindings.cpp` | `BindingsConfig`, `InstallAll` |
| `script_common.hpp` | Espace `scriptlib` : `Options` (lecture typée d'une table d'options, clés inconnues refusées), `Vec3Of`, `Vec3List`, `QuatList`, `BytesOf`, `BytesList` |
| `script_color.hpp` | `ColorOf`, `FloatColor`, `ByteColor` : `"#rrggbb"`, `"#rrggbbaa"`, `[r, g, b(, a)]` (0–1, ou 0–255 dès qu'une composante dépasse 1) |
| `script_archive.hpp`, `script_audio.hpp`, `script_dataio.hpp`, `script_ecs.hpp`, `script_generator.hpp`, `script_physics.hpp`, `script_render3d.hpp`, `script_sql.hpp`, `script_ui.hpp` | Une bibliothèque par module ; l'en-tête de chacun décrit l'API vue du script |

Dépendances : `script`, `ecs`, `ui`, `generators`, `audio`, `archive`,
`physics`, `sql`, `dataio`, `render3d`.

> Le commentaire de `script_bindings.hpp` mentionne des fichiers
> `script_jobs.hpp`, `script_platform.hpp` et `script_resources.hpp`.
> **Ils n'existent pas dans le dépôt** : `jobs`, `platform` et `resources`
> n'ont pas de liaisons Script.

## API vue des scripts (extraits des en-têtes)

**`ecs`**

- `ecs.world()` crée un monde propre au script ; `ecs.host()` rend le monde
  de l'hôte s'il est exposé.
- `monde.spawn({composants})`, `monde.system(nom, [composants], fn(e, …, dt))`,
  `monde.run(dt)`, `monde.query("pos")`, `e.get(nom)`.
- Les composants du script sont des valeurs rangées sous un nom. Ceux de
  l'hôte sont déclarés par `EcsWorld::BindComponent`.
- Les requêtes matérialisent la liste d'entités : un système peut créer ou
  détruire des entités sans risque.

**`ui`**

- Fabriques : `ui.column(opts)`, `ui.label(texte, opts)`,
  `ui.button(texte, fn, opts)`, `ui.progress(min, max, opts)`…
- Options communes : `parent`, `name`, `class`, `style`, `w`, `h`, `grow`,
  `pad`, `gap`, `bg`, `color`, `border`, `radius`, `font_size`, `bold`,
  `align`, `justify`, `anchor`, `absolute`, `hidden`…
- Sans écran, les widgets restent des objets fonctionnels : `w.click()`
  simule un clic.
- Les rappels déclenchés par l'utilisateur sont mis en file et exécutés par
  `UiBinding::Flush()`, que l'hôte appelle une fois par image.

**`gen`**

- `gen.noise({type, fractal, octaves, frequency, seed})` ;
- `gen.heightmap(w, h).fill(bruit).normalize().terrace(…)` ;
- labyrinthes, dispersion…

**`sql`**

- `sql.database()` crée une base possédée par le script ; `sql.host()` rend
  celle de l'hôte.
- Méthodes : `execute(sql)`, `insert`, `upsert`, `find`, `delete`, `select`,
  `tables`, `columns`, `index`, `lookup`.

**Autres espaces**

- `audio` : `audio.play(chemin, {volume, position})`, `audio.fade(v, volume, s)` ;
- `physics`, `render3d`, `archive` et `dataio` suivent le même modèle (voir
  l'en-tête de chacun).

## Exemple

```cpp
#include <cstdio>
#include <script/script.hpp>
#include <script_bindings/script_bindings.hpp>

static const char *SOURCE = R"script(
// ECS : un monde propre au script, un système par composants nommés
let monde = ecs.world()
monde.spawn({pos: [0, 0], vit: [2, 1]})
monde.spawn({pos: [5, 5], vit: [0, -1]})
monde.system("mouvement", ["pos", "vit"], fn(e, pos, vit, dt) {
    pos[0] += vit[0] * dt
    pos[1] += vit[1] * dt
})
monde.run(0.5)
for (e in monde.query("pos")) { print("pos " .. e.get("pos")[0] .. ", " .. e.get("pos")[1]) }

// Génération procédurale
let bruit = gen.noise({type: "simplex", octaves: 4, frequency: 0.05, seed: 7})
let carte = gen.heightmap(32, 32).fill(bruit).normalize()
print("hauteur au centre : " .. carte.get(16, 16))

// Base SQL embarquée (SQL brut ou méthodes)
let db = sql.database()
db.execute("CREATE TABLE score (id INT PRIMARY KEY, nom STRING, points INT)")
db.insert("score", {id: 1, nom: "Ada", points: 120})
db.execute("INSERT INTO score VALUES (2, 'Bob', 80)")
for (ligne in db.select("score")) { print(ligne.nom .. " : " .. ligne.points) }
print("id 1 → " .. db.find("score", 1).nom)
)script";

int main() {
	script::Interpreter vm;
	vm.onPrint = [](const String &line) { std::printf("%s\n", line.CStr()); };

	script::BindingsConfig config;
	config.generators = true;
	config.dataio = true;
	config.ecsWorld = std::make_shared<script::EcsWorld>(); // active la bibliothèque `ecs`
	sql::Database db;                                        // `sql` a besoin d'une base hôte
	config.sqlDatabase = &db;
	for (const String &ns : script::InstallAll(vm, config))
		std::printf("installé : %s\n", ns.CStr());

	auto result = vm.Run(SOURCE);
	if (result.IsError()) {
		std::fprintf(stderr, "%s\n", result.Error().Format().CStr());
		return 1;
	}
	return 0;
}
```

Sortie :

```
installé : ecs
installé : gen
installé : dataio
installé : sql
pos 1, 0.5
pos 5, 4.5
hauteur au centre : 0.579699
Ada : 120
Bob : 80
id 1 → Ada
```

## Intégration

```toml
[dependencies]
modules = ["script_bindings"]   # tire script, ui, render3d, physics, sql…
```

**Durée de vie** : les objets de l'hôte passés aux bibliothèques
(`AudioEngine`, `physics::World`, `Canvas`, `sql::Database`, monde ECS) ne
sont **pas possédés** par l'interpréteur. Ils doivent lui survivre.

**Fils d'exécution** : le monde ECS de l'hôte n'est manipulé que sur le fil
principal ; ses méthodes y sont renvoyées depuis un fil `async`.

## Tests

```sh
cpp-maker test -m script_bindings
```

`test_script_bindings.cpp` et `test_script_audio.cpp` exécutent des scripts
contre chaque bibliothèque, sans fenêtre.
