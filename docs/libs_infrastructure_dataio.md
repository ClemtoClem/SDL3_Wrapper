# dataio — formats de données sur un arbre commun

`libs/infrastructure/dataio` · couche **infrastructure** · `libdataio.a` · espace de noms `dataio::` (et `dataio::gltf::`)

## Présentation

`dataio` lit et écrit plusieurs formats de données textuels à travers **un
seul arbre**, `dataio::Node`, indépendant du format :

| Format | Classe | Extensions | Nom dans le registre |
|---|---|---|---|
| JSON (RFC 8259) | `JsonDocument` | `.json` | `json` |
| XML 1.0 (sous-ensemble) | `XmlDocument` | `.xml` | `xml` |
| HTML (sous-ensemble HTML5) | `HtmlDocument` | `.html`, `.htm` | `html` |
| YAML (style bloc) | `YamlDocument` | `.yaml`, `.yml` | `yaml` |
| TOML (sous-ensemble) | `TomlDocument` | `.toml` | `toml` |
| INI | `IniDocument` | `.ini`, `.cfg` | `ini` |
| CSV (RFC 4180) | `CsvDocument` | `.csv` | `csv` |
| CSS | `CssDocument` | `.css` | `css` |

Un document décodé dans un format peut ainsi être réécrit dans un autre. Le
module ajoute deux fonctions plus spécialisées :

- la **lecture et l'écriture glTF 2.0** (`dataio::gltf`) : maillages,
  matériaux, nœuds et animations, tampons binaires décodés ;
- l'**export de maillages triangulés** en Wavefront OBJ (+ MTL), STL
  (ASCII et binaire) et PLY.

Le module n'utilise aucune exception. Le décodage rend
`Option<ParseError>` (`NONE` = succès), avec la ligne et la colonne de
l'erreur.

**Utilisateurs principaux** :

- les feuilles de style CSS de [ui](libs_infrastructure_ui.md) ;
- les contenus JSON de [voxel](libs_infrastructure_voxel.md) ;
- le glTF de [render3d](libs_infrastructure_render3d.md) ;
- les projets de la [sandbox](demo_sandbox.md) ;
- le format `.vcap` du [profiler](demo_profiler.md) ;
- l'export de [modeling](libs_infrastructure_modeling.md).

## Architecture interne

| Fichier | Contenu |
|---|---|
| `dataio.hpp` | En-tête parapluie (tous les codecs) |
| `node.hpp`, `src/node.cpp` | `NodeType`, `Node`, `NodePtr` |
| `document.hpp`, `src/document.cpp` | `ParseError`, `Reader` (curseur de lecture), `scalar::`, `Document` (base), `DocumentFactory` (registre), macro `DOC_REGISTER_FORMAT` |
| `documents/*.hpp`, `src/documents/*.cpp` | Un codec par format, plus `gltf.hpp` et `gltf_writer.hpp` |
| `mesh_export.hpp`, `src/mesh_export.cpp` | `TriangleMeshView`, `EncodeObj`, `EncodeMtl`, `EncodeStlAscii`, `EncodeStlBinary`, `EncodePly`, `SaveMesh` |

Dépendances :

- `core` ;
- `sdl3` : les nœuds composés sont stockés dans un `sdl3::Properties`, et
  `Decode` et `Encode` travaillent sur un `sdl3::IOStream`.

## API publique

### `Node`

- **`type`** : `NodeType`, parmi `NONE`, `OBJECT`, `ARRAY`,
  `STRING`, `BOOL`, `INT` et `FLOAT`.
- **Valeurs scalaires** : champs publics `stringValue`, `boolValue`,
  `intValue` (int64) et `floatValue` (double).
- **Fabriques** : `MakeNone`, `MakeObject`, `MakeArray`, `MakeString`,
  `MakeBool`, `MakeInt` et `MakeFloat`. Elles rendent un `NodePtr`
  (`std::shared_ptr<Node>`).
- **Prédicats** : `IsObject`, `IsArray`, `IsString`, `IsNumber`,
  `IsScalar`…
- **Objet** : `Set(clé, enfant)`, `Get(clé)` (rend `nullptr` si la clé
  est absente), `Has`, `Remove`, `Keys()`.
- **Tableau** : `Push(enfant)`, `GetSize()`, `At(i)`.
- **`Clone()`** : copie profonde.

L'ordre d'insertion des clés est conservé (vecteur `order`). Cela garantit
des allers-retours stables, ce qui compte pour XML, pour les colonnes CSV et
pour la lisibilité de JSON et YAML.

### `Document` (base de tous les codecs)

| Membre | Rôle |
|---|---|
| `Option<ParseError> Decode(sdl3::IOStream&)` / `DecodeStr(texte)` | Décode et remplit la racine |
| `bool Encode(sdl3::IOStream&) const` / `String EncodeStr() const` | Sérialise la racine |
| `GetRoot()` / `SetRoot(NodePtr)` | Arbre |

`ParseError {message, line, column}` ; `Format()` rend un message lisible.

### `DocumentFactory`

- `instance()` rend le singleton.
- `CreateByName("yaml")` et `CreateByFilename("a.yml")` rendent `nullptr`
  si le format est inconnu.
- `RegisteredFormats()` liste les formats.
- `registerFormat(nom, extensions, créateur)` enregistre un format. La macro
  `DOC_REGISTER_FORMAT(nom, Classe, ".ext"…)` le fait automatiquement.

### Correspondance des formats avec l'arbre (limites)

| Format | Particularités |
|---|---|
| XML | Attributs sous la clé `@attributes`, texte sous `#text`. Deux enfants de même balise sont fusionnés en tableau : l'ordre d'entrelacement entre balises de noms différents est perdu. Pas de DTD, CDATA, espaces de noms ni entités personnalisées |
| HTML | Logique de XML, plus les balises vides, les attributs booléens ou non cités, la fermeture implicite et le contenu brut de `<script>`/`<style>` ; doctype sous `@doctype` |
| YAML | Style bloc seulement. Non pris en charge : style « flow » (`[a, b]`, `{k: v}`), ancres et alias, tags, scalaires `|` et `>`, clés complexes, documents multiples (les `---` sont ignorés). `- clé: valeur` se lit, mais l'encodeur écrit la forme développée |
| TOML | Tables et tables imbriquées `[a.b]`, `clé = valeur`, tableaux en ligne de scalaires. Pas de clés pointées, tables en ligne, `[[tableaux de tables]]`, dates ni chaînes multilignes |
| INI | Deux niveaux (section → clé) ; les clés avant la première section sont à la racine ; commentaires `;` et `#` |
| CSV | La racine est un objet dont chaque valeur est un tableau (une colonne nommée par l'en-tête) |
| CSS | La racine est un **tableau** de règles (sélecteurs et règles-@ peuvent se répéter) |

Les formats texte typent automatiquement leurs scalaires : bool, puis int,
puis float, sinon chaîne (`scalar::ParseNode`).

### glTF (`documents/gltf.hpp`, `gltf_writer.hpp`, espace `dataio::gltf`)

**Lecture**

- `Result<Document, String> LoadFile(path, loadBuffers = true)` charge un
  fichier. `ParseJson(racine, dossier)` analyse un arbre déjà décodé, et
  `LoadBuffers(doc)` charge ensuite les tampons.
- `gltf::Document` expose les tableaux de la spécification : `buffers`,
  `bufferViews`, `accessors`, `meshes` (primitives et attributs),
  `nodes` (TRS), `scenes`, `materials` (PBR), `images`, `textures`.
- `ReadFloats(accesseur)` et `ReadIndices(accesseur)` décodent les données.
  Toutes les bornes sont vérifiées : un fichier tronqué donne une erreur,
  jamais une lecture hors tampon. `ResolveUri` résout un chemin relatif au
  document.
- Non pris en charge : le conteneur `.glb` et les tampons embarqués en
  base64, signalés explicitement comme erreurs. Les accesseurs *sparse* non
  plus.

**Écriture**

- On décrit la scène avec `SceneDescription` (meshes, materials, nodes,
  animations).
- `ValidateScene(scène)` la vérifie, et `SaveScene(chemin, scène)` écrit
  le `.gltf` et son `.bin`. Les fichiers produits se relisent dans
  Blender et Blockbench.

### Export de maillages (`mesh_export.hpp`)

- **Description du maillage** : `TriangleMeshView` contient des `span`
  sur des tableaux plats : `positions` (obligatoire), `normals`, `uvs`,
  `colors`, `indices` et `materials` (`MeshMaterialGroup`).
- **Encodage en mémoire** : `EncodeObj(mesh, ObjOptions)`, `EncodeMtl`,
  `EncodeStlAscii`, `EncodeStlBinary` (normales recalculées par triangle)
  et `EncodePly`.
- **Écriture d'un fichier** : `SaveMesh(chemin, mesh, commentaire)` choisit
  le format d'après l'extension (`.obj`, `.stl`, `.ply`) et écrit le
  `.mtl` d'un OBJ avec matériaux. Il rend `NONE` en cas de succès, sinon
  le message d'erreur.

## Exemple

```cpp
#include <cstdio>
#include <dataio/dataio.hpp>
#include <dataio/mesh_export.hpp>

int main() {
	// 1. YAML → arbre commun
	String yaml = "player:\n  name: Ada\n  level: 7\n  speed: 4.5\n  items:\n    - sword\n    - shield\n";
	dataio::YamlDocument in;
	if (Option<dataio::ParseError> err = in.DecodeStr(yaml); err.IsSome()) {
		std::fprintf(stderr, "%s\n", err->Format().CStr());
		return 1;
	}
	dataio::NodePtr player = in.GetRoot()->Get("player");
	std::printf("%s niveau %lld, %zu objets\n", player->Get("name")->stringValue.CStr(),
				static_cast<long long>(player->Get("level")->intValue), player->Get("items")->GetSize());

	// 2. Modification de l'arbre puis export dans un autre format
	player->Set("alive", dataio::Node::MakeBool(true));
	dataio::JsonDocument json;
	json.SetRoot(in.GetRoot());
	std::printf("%s\n", json.EncodeStr().CStr());

	// 3. Choix du codec d'après l'extension
	dataio::DocumentPtr toml = dataio::DocumentFactory::instance().CreateByFilename("config.toml");
	toml->SetRoot(in.GetRoot());
	std::printf("%s", toml->EncodeStr().CStr());

	// 4. Export d'un maillage (un triangle) en OBJ et STL
	std::vector<float> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
	std::vector<uint32_t> indices{0, 1, 2};
	dataio::TriangleMeshView mesh;
	mesh.positions = positions;
	mesh.indices = indices;
	std::printf("%s", dataio::EncodeObj(mesh).CStr());
	Option<String> saveErr = dataio::SaveMesh("triangle.stl", mesh);
	std::printf("STL écrit : %d\n", saveErr.IsNone());
	return 0;
}
```

Sortie (extrait) :

```
Ada niveau 7, 2 objets
{
  "player": {
    "name": "Ada",
    "level": 7,
    "speed": 4.5,
    "items": [
      "sword",
      "shield"
    ],
    "alive": true
  }
}

[player]
name = "Ada"
level = 7
…
```

Lecture d'un fichier :

```cpp
auto io = sdl3::IOStream::FromFile("config.xml", "rb");
if (io) {
	dataio::XmlDocument xml;
	if (auto err = xml.Decode(io.Value()); err.IsSome())
		std::printf("%s
", err->Format().CStr()); // message avec numéro de ligne
}
```

## Intégration

`modules = ["dataio"]`, puis `#include <dataio/dataio.hpp>` pour tous les
codecs, ou l'en-tête précis (`<dataio/documents/json.hpp>`).

> Le format d'un fichier n'est enregistré dans `DocumentFactory` que si
> l'en-tête de son codec est inclus. `dataio.hpp` les inclut tous.

## Tests

```sh
cpp-maker test -m dataio
```

Les tests (`test_data_1.cpp` à `test_data_9.cpp`, `test_document.cpp`,
`test_gltf.cpp`, `test_mesh_export.cpp`) couvrent les allers-retours de
chaque format, les erreurs de syntaxe, glTF et l'export de maillages.
