# modeling — modélisation 3D à la SketchUp

`libs/infrastructure/modeling` · couche **infrastructure** · `libmodeling.a` · espace de noms `modeling::`

## Présentation

`modeling` fournit un **maillage éditable** et les outils pour le
modifier, à la manière de SketchUp.

- **Le maillage** (`Model`) est fait de sommets, d'arêtes et de **faces
  planes**. Une face a un contour extérieur et d'éventuels trous, et porte un
  matériau et un calque.
- **Les opérations** reprennent les outils de SketchUp : Pousser/Tirer,
  Décalage, Suivez-moi, Déplacer, Rotation, Échelle, Gomme, Pot de peinture,
  Adoucir les arêtes. S'y ajoutent l'extrusion et la révolution d'openskp.
- **Le moteur d'inférence** accroche le point sous le curseur à ce qui a du
  sens : extrémité, milieu, arête, face, axes rouge/vert/bleu, origine, sol.
- **Les outils interactifs** sont des machines à états pures. Ils reçoivent
  les clics et la saisie des cotes (« boîte de mesures »), et décrivent leur
  aperçu. Ils ne dépendent d'aucune fenêtre.
- **Les primitives paramétriques** redéveloppent ezGeometry : pyramide,
  demi-sphère, capsule, escaliers droits et hélicoïdaux, arche, rampe.
- **La triangulation** prépare l'affichage, et le lancer de rayon sert à la
  sélection.
- **Les entrées/sorties** :
  - format natif `.mesh` (JSON) ;
  - export OBJ (+ MTL), STL et PLY ;
  - import de n'importe quel maillage triangulé.

Le repère a l'axe Y vers le haut (comme `render3d`), et l'unité est le
mètre.

Le module est utilisé par l'éditeur de maillages de la
[sandbox](demo_sandbox.md) (`demos/sandbox/src/editor/modeling_*.cpp`) et
par sa bibliothèque de maillages.

## Architecture interne

| Fichier | Contenu |
|---|---|
| `model.hpp`, `src/model.cpp` | `Vec3`, `VertexId`/`EdgeId`/`FaceId`, `Color`, `Material`, `Layer`, `Vertex`, `Edge`, `Face`, `FaceOptions`, `CurveOptions`, `PlaneInfo`, `Model` |
| `operations.hpp`, `src/operations.cpp` | `PushPull`, `Offset`, `FollowMe`, `Extrude`, `Revolve`, `ExtractFaces`, `Move`, `Rotate`, `Scale`, `Erase`, `Paint`, `SoftenEdges` |
| `primitives.hpp`, `src/primitives.cpp` | `AddPyramid`, `AddHalfSphere`, `AddCapsule`, `AddStairs`, `AddSpiralStairs`, `AddArch`, `AddRamp` |
| `inference.hpp`, `src/inference.cpp` | `InferenceKind`, `Inference`, `InferenceQuery`, `InferenceSettings`, `Infer`, `InferenceColor` |
| `tools.hpp`, `src/tools.cpp` | `ToolKind`, `Selection`, `ToolEvent`, `ToolContext`, `ToolPreview`, `ToolResult`, `Tool`, `MakeTool`, `ParseMeasurement`, `FormatLength`, `FormatAngle` |
| `triangulate.hpp`, `src/triangulate.cpp` | `MeshData`, `MeshGroup`, `BuildOptions`, `BuildMesh`, `VisibleEdges`, `RayHit`, `Raycast` |
| `io.hpp`, `src/io.cpp` | `ToJson`/`FromJson`, `Serialize`/`Deserialize`, `SaveModel`/`LoadModel`, `ExportModel`, `ImportOptions`, `FromTriangles` |

Dépendances :

- `core` ;
- `math` : vecteurs, matrices et triangulation de polygones (`math::TriangulatePolygon`) ;
- `dataio` : JSON et export de maillages.

## API publique

### `Model`

**Identifiants**

`VertexId`, `EdgeId` et `FaceId` sont des index **stables**. Supprimer un
élément le marque mort sans décaler les autres : les outils et l'annulation
peuvent donc garder leurs références. `Compact()` renumérote ; il est appelé
avant l'enregistrement.

**Construction** (calquée sur le writer d'openskp)

- matériaux et calques : `AddMaterial(nom, couleur)` et `AddLayer(nom)` ;
- géométrie élémentaire : `AddVertex`, `AddEdge` ;
- faces : `AddFace(contour, trous, FaceOptions)` et `AddFaceFromVertices` ;
- courbes et formes : `AddPolyline`, `AddCircle`, `AddPolygon`,
  `AddArc`, `AddRectangle(coin, côté1, côté2)`.

**Orientation**

- Le contour extérieur tourne dans le sens trigonométrique vu depuis la
  normale ; `AddFace` corrige l'orientation fournie.
- Pour `AddRectangle`, la normale est `côté1 × côté2`.

**Requêtes**

- comptes : `VertexCount`, `EdgeCount`, `FaceCount` ;
- recherche et voisinage : `FindEdge`, `FindVertex`, `FacesOfEdge`,
  `EdgesOfFace` ;
- géométrie d'une face : `FacePlane`, `FaceArea`, `FaceContains` ;
- boîte englobante : `Bounds`.

**Édition**

- suppression : `RemoveFace`, `RemoveEdge`, `RemoveOrphanVertices` ;
- faces : `FlipFace`, `MergeCoplanar`, `Imprint`, `SplitFace`,
  `SplitFaceAlong` ;
- arêtes et sommets : `SplitEdge`, `WeldVertices`, `SetEdgeSmoothing` ;
- transformations : `Transform`, `TransformVertices` ;
- assemblage : `Append(autre, transform)`.

> Après une modification **structurelle** faite par `Vertices()`,
> `Edges()` ou `Faces()` en écriture, appelez `InvalidateIndices()`.
> Changer un matériau ou un drapeau ne le demande pas.

### Opérations (`operations.hpp`)

Toutes rendent `Result<…, String>`. Le message d'erreur est destiné à
l'utilisateur.

- `PushPull(model, face, distance, {keepOriginal})` : un `keepOriginal`
  à vrai crée un étage, comme Ctrl dans SketchUp. La fonction rend la face
  déplacée.
- `Offset(model, face, distance)`, `FollowMe(model, profil, chemin)`.
- `Extrude(model, contour, vecteur, …)`, `Revolve(model, origine, axe, profil, RevolveOptions)`.
- `Move`, `Rotate`, `Scale` sur des sommets ; `VerticesOf(model, faces)` les rassemble.
- `Erase(model, arêtes, faces)`, `Paint(model, face, matériau)`, `SoftenEdges(model, angleMax)`.
- `ExtractFaces(model, faces)` rend un nouveau `Model`.

### Inférence et outils

- **`Infer(model, InferenceQuery, InferenceSettings)`** rend une `Inference` :
  - un genre : `GROUND`, `ON_FACE`, `ON_EDGE`, `MIDPOINT`,
    `ENDPOINT`, `ORIGIN`, `ON_AXIS_X/Y/Z`, `LOCKED_AXIS` ou
    `ON_PLANE` ;
  - un point et une étiquette ;
  - une couleur, donnée par `InferenceColor`.

  L'appelant fournit le rayon de la souris et une fonction de projection à
  l'écran. Le module ne dépend donc d'aucune caméra.
- **`MakeTool(ToolKind)`** crée un outil :
  - interface `Tool` : `Hover`, `Click`, `Type(cotes)`, `Preview`,
    `Reset`, `IsActive` ;
  - genres : `SELECT`, `LINE`, `RECTANGLE`, `CIRCLE`, `POLYGON`,
    `PUSH_PULL`, `OFFSET`, `FOLLOW_ME`, `MOVE`, `ROTATE`, `SCALE`,
    `ERASER`, `PAINT`, `TAPE`.
- **Raccourcis** de SketchUp (`ToolShortcut`/`ToolFromShortcut`) :
  - Espace : sélection ; L : ligne ; R : rectangle ; C : cercle ;
  - P : pousser/tirer ; F : décalage ;
  - M : déplacer ; Q : rotation ; S : échelle ;
  - E : gomme ; B : peinture ; T : mètre.
- **Cotes** : `ParseMeasurement("2,5")`, `FormatLength`, `FormatAngle`.

### Affichage et sélection (`triangulate.hpp`)

- **`BuildMesh(model, BuildOptions)`** produit un `MeshData` :
  - les tableaux `positions`, `normals`, `uvs`, `colors` et
    `indices` ;
  - des groupes par matériau (un appel de dessin chacun) ;
  - `triangleFace` : la face d'origine de chaque triangle.

  Les normales ne sont partagées qu'à travers une arête **lisse**.
- **`VisibleEdges(model)`** rend les arêtes à tracer.
- **`Raycast(model, rayon)`** rend `Option<RayHit>` (face, distance,
  point).

## Exemple

```cpp
#include <cstdio>
#include <modeling/io.hpp>
#include <modeling/operations.hpp>
#include <modeling/primitives.hpp>
#include <modeling/triangulate.hpp>

using modeling::Vec3;

int main() {
	modeling::Model model;
	uint32_t brick = model.AddMaterial("Brique", {0.7f, 0.3f, 0.2f, 1.f});

	// Un rectangle au sol (normale = côté1 × côté2, ici vers le haut),
	// « poussé » en volume de 2,5 m (Pousser/Tirer)
	Option<modeling::FaceId> floor = model.AddRectangle({0, 0, 0}, {0, 0, 3}, {4, 0, 0});
	if (floor.IsNone())
		return 1;
	auto top = modeling::PushPull(model, floor.Unwrap(), 2.5f);
	if (!top) {
		std::printf("Pousser/Tirer : %s\n", top.Error().CStr());
		return 1;
	}
	modeling::Paint(model, top.Value(), brick);
	std::printf("boîte : %zu faces, %zu arêtes, %zu sommets\n", model.FaceCount(), model.EdgeCount(), model.VertexCount());

	// Une primitive paramétrique ajoutée au même modèle
	auto stairs = modeling::AddStairs(model, {1.f, 1.f, 2.f}, 5);
	std::printf("escalier : %zu faces\n", stairs ? stairs.Value().size() : 0);

	// Triangulation pour l'affichage, lancer de rayon pour la sélection
	modeling::MeshData mesh = modeling::BuildMesh(model);
	std::printf("affichage : %zu triangles en %zu groupes\n", mesh.TriangleCount(), mesh.groups.size());
	math::FRay ray{{2.f, 10.f, 1.5f}, {0.f, -1.f, 0.f}};
	if (auto hit = modeling::Raycast(model, ray); hit.IsSome())
		std::printf("rayon : face %u à %.2f m\n", hit->face, hit->distance);

	// Format natif (.mesh, JSON) et export d'échange (OBJ + MTL)
	if (auto err = modeling::SaveModel("maison.mesh", model); err.IsSome())
		std::printf("%s\n", err->CStr());
	(void)modeling::ExportModel("maison.obj", model);
	auto reloaded = modeling::LoadModel("maison.mesh");
	std::printf("relu : %zu faces\n", reloaded ? reloaded.Value().FaceCount() : 0);
	return 0;
}
```

Sortie :

```
boîte : 6 faces, 12 arêtes, 8 sommets
escalier : 14 faces
affichage : 52 triangles en 2 groupes
rayon : face 5 à 7.50 m
relu : 20 faces
```

## Intégration

`modules = ["modeling"]`. Le module est purement géométrique : il ne
dépend ni de SDL ni du GPU.

Pour l'afficher :

1. convertir `MeshData` en `render3d::Mesh` (comme le fait la sandbox) ;
2. dessiner l'aperçu de l'outil actif (`ToolPreview`) par-dessus.

## Tests

```sh
cpp-maker test -m modeling
```

- `test_model.cpp` : construction, orientation et découpes ;
- `test_tools.cpp` : outils pilotés par des clics simulés ;
- `test_io_primitives.cpp` : formats et primitives.
