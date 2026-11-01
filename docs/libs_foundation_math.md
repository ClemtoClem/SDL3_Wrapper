# math — mathématiques 2D/3D

`libs/foundation/math` · couche **foundation** · `libmath.a` · espace de noms `math::`

## Présentation

`math` regroupe la géométrie dont ont besoin le rendu, la physique, l'UI, la
modélisation et le moteur voxel :

- vecteurs, matrice 4×4 et quaternions ;
- volumes englobants, plans, rayons et frustums ;
- transformations locales 2D et 3D ;
- polygones 2D, y compris la triangulation avec trous ;
- marges et rayons de coins (`Sides`, `Corners`).

Le module ne dépend de rien, ni de SDL ni de `core`. Les types SDL
(`SDL_FPoint`…) se convertissent depuis et vers ces types du côté du module
[`sdl3`](libs_infrastructure_sdl3.md) : `sdl3` dépend de `math`, jamais
l'inverse.

## Architecture interne

| Fichier | Contenu |
|---|---|
| `include/math/math.hpp`, `src/math.cpp` | Fonctions scalaires, `Corners`, `Sides`, `FVector2/3/4`, `FMatrix4`, `FQuaternion`, `TRS`, `ComposeTRS`/`DecomposeTRS`, `FAABB`, `FPlane`, `FRay`, `FFrustum` |
| `include/math/transform.hpp`, `src/transform.cpp` | `Transform3D`, `Transform2D` |
| `include/math/polygon.hpp`, `src/polygon.cpp` | `SignedArea`, `OnPolygonBorder`, `PointInPolygon`, `TriangulatePolygon` |

## API publique

### Fonctions scalaires

`Sqrt`, `Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan`, `Atan2` (sur `float`) et
les gabarits constexpr `Abs`, `Min`, `Max`, `Clamp`.

### Vecteurs

| Type | Membres principaux |
|---|---|
| `FVector2 {x, y}` | `Dot`, `LengthSq`, `Length`, `Normalize`, `Lerp`, `Distance(Sq)`, opérateurs arithmétiques |
| `FVector3 {x, y, z}` | Les mêmes, plus `Cross`, `Reflect(n)`, `static Min/Max(a, b)`, `XY()` ; construction depuis `(FVector2, z)` |
| `FVector4 {x, y, z, w}` | `Dot`, `Length`, `Normalize`, `Lerp`, `XYZ()`, `XY()` ; `w = 0` par défaut (direction), utiliser `FVector4(v, 1.f)` pour un point |

### `FMatrix4`

Matrice 4×4 stockée **par colonnes** (`m[col * 4 + row]`, comme `mat4` en
GLSL). Les projections visent l'espace de découpe **Vulkan** (Y vers le bas,
Z ∈ [0, 1]), celui de SDL_GPU.

- Constructeurs statiques :
  - `Identity` ;
  - `Translate`, `Scale` ;
  - `RotateX/Y/Z`, `Rotate(axe, angle)` ;
  - `Perspective(fovY, aspect, near, far)` (fovY en radians) ;
  - `Ortho(...)` ;
  - `LookAt(œil, cible, haut)`.
- Méthodes :
  - `At(row, col)` ;
  - `TransformPoint` (w = 1) et `TransformDir` (w = 0) ;
  - `Transpose`, `Inverse` ;
  - produit par `*`.

### `FQuaternion`

- Fabriques : `Identity`, `FromAxisAngle(axe, angle)`, `FromEuler(pitch, yaw, roll)`, `FromTo(de, vers)` (plus courte rotation) et `FromMatrix`.
- Méthodes : `Conjugate`, `Norm`, `Normalize`, `Rotate(v)`, `ToMat4`, `ToEuler`, `Slerp(vers, t)`.

### Composition et décomposition

- `FMatrix4 ComposeTRS(translation, rotation, échelle)`.
- `TRS DecomposeTRS(const FMatrix4&)` rend la translation, la rotation et
  l'échelle. Deux cas limites :
  - un miroir est porté sur X, et un cisaillement est approché par le repère
    orthonormalisé le plus proche, avec `sheared = true` ;
  - une échelle nulle sur un axe remplace cet axe par l'axe identité.

### Volumes et requêtes

| Type | Membres |
|---|---|
| `FAABB {min, max}` | Vide par défaut (`min > max`), `IsValid`, `Center`, `HalfExtents`, `Size`, `Contains(point / boîte)`, `Intersects`, `Expand(point / boîte)`, `Translated`, `Transformed(mat)` |
| `FPlane {normal, d}` | `Distance(p)` (signée), `Normalize` |
| `FRay {origin, direction}` | `At(t)`, `DistanceTo(p)`, `Intersects(FAABB, tMin, tMax)`, `Intersects(FPlane, t)`, `Intersects(v0, v1, v2, …)` (triangle), `Transformed(mat)` |
| `FFrustum` | `static FromViewProj(vp)`, `Intersects(FAABB)` (élimination hors champ) |

### `Corners` et `Sides`

- **`Corners {tl, tr, bl, br}`** : rayons des quatre coins.
- **`Sides {left, top, right, bottom}`** : marges et bordures. `H()` rend
  `left + right` et `V()` rend `top + bottom`.

Le module [`ui`](libs_infrastructure_ui.md) et `sdl3` les réutilisent
(`sdl3::Corners`/`Sides` sont des alias).

### Transformations (`transform.hpp`)

- **`Transform3D {position, rotation, scale}`** : `Identity`, `Matrix()` (T×R×S), `FromMatrix`, `EulerDegrees`/`SetEulerDegrees`, `ComposedWith(parent)`. Constantes `DEG2RAD`, `RAD2DEG`.
- **`Transform2D {position, rotationDegrees, scale}`** : `Matrix()` (plan XY), `Apply(point local)`, `To3D()`.

Ces transformations ne portent ni hiérarchie ni identifiant : c'est le rôle du
graphe de scène ([`scene`](libs_application_scene.md)).

### Polygones (`polygon.hpp`)

| Fonction | Rôle |
|---|---|
| `float SignedArea(polygone)` | Aire signée, positive dans le sens trigonométrique |
| `bool OnPolygonBorder(polygone, p, tolérance)` | Le point est-il sur un côté ? |
| `bool PointInPolygon(polygone, p)` | Intérieur strict, selon la règle pair-impair |
| `bool TriangulatePolygon(contour, trous, outIndices)` | Découpe d'oreilles avec trous raccordés par pont (méthode d'Eberly). Les indices se rapportent à la concaténation `[contour, trous[0], …]`, les triangles sont dans le sens trigonométrique, et le sens des contours fournis est indifférent. Rend faux si rien n'a pu être triangulé. |

Ces fonctions servent à `render3d` (`Shape2D`, extrusion) et à `modeling`
(faces des maillages éditables).

## Exemple

```cpp
#include <cstdio>
#include <math/math.hpp>
#include <math/polygon.hpp>
#include <math/transform.hpp>

int main() {
	using namespace math;
	FMatrix4 proj = FMatrix4::Perspective(60.f * Transform3D::DEG2RAD, 16.f / 9.f, 0.1f, 100.f);
	FMatrix4 view = FMatrix4::LookAt({0.f, 2.f, 5.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
	FFrustum frustum = FFrustum::FromViewProj(proj * view);

	FAABB box;
	box.Expand(FVector3{-1.f, 0.f, -1.f});
	box.Expand(FVector3{1.f, 2.f, 1.f});
	std::printf("visible : %d\n", frustum.Intersects(box)); // 1

	FRay ray{{0.f, 1.f, 10.f}, {0.f, 0.f, -1.f}};
	float tMin = 0.f, tMax = 0.f;
	if (ray.Intersects(box, tMin, tMax))
		std::printf("entrée à t = %.1f\n", tMin); // 9.0

	Transform3D parent;
	parent.position = {0.f, 1.f, 0.f};
	Transform3D child;
	child.SetEulerDegrees({0.f, 90.f, 0.f});
	child.position = {2.f, 0.f, 0.f};
	FVector3 world = child.ComposedWith(parent).Matrix().TransformPoint({0.f, 0.f, 0.f}); // (2, 1, 0)

	FQuaternion b = FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, 3.14159265f / 2.f);
	FVector3 mid = FQuaternion::Identity().Slerp(b, 0.5f).Rotate({1.f, 0.f, 0.f}); // (0.71, 0, -0.71)

	std::vector<FVector2> square{{0, 0}, {4, 0}, {4, 4}, {0, 4}};
	std::vector<std::vector<FVector2>> holes{{{1, 1}, {3, 1}, {3, 3}, {1, 3}}};
	std::vector<uint32_t> indices;
	if (TriangulatePolygon(square, holes, indices))
		std::printf("aire %.0f, %zu triangles\n", SignedArea(square), indices.size() / 3); // 16, 8
	(void)world;
	(void)mid;
}
```

## Intégration

`modules = ["math"]` dans `[dependencies]`, puis `#include <math/math.hpp>`.
Il n'y a rien à initialiser.

## Tests

```sh
cpp-maker test -m math
```
