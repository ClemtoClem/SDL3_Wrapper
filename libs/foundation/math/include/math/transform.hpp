#pragma once
/**
 * Transformations locales : ce qu'un nœud porte par rapport à son parent.
 *
 *  - `Transform3D` : position, rotation (quaternion — elle se compose et
 *    s'interpole sans blocage de cardan), échelle ; matrice TRS.
 *  - `Transform2D` : position et échelle dans le plan, rotation autour de Z
 *    en degrés ; matrice dans le plan XY (z = 0), pour se composer avec les
 *    matrices 3D quand un calque 2D en a besoin.
 *
 * Ni identifiant ni hiérarchie ici : c'est le graphe de scène qui les porte
 * (cf. scene::Node). Ni format de fichier : la sérialisation lit et écrit ces
 * champs (cf. scene::Transform::ToJson).
 */
#include "math.hpp"

namespace math {

struct Transform3D {
	FVector3 position;
	FQuaternion rotation = FQuaternion::Identity();
	FVector3 scale{1.f, 1.f, 1.f};

	static constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;
	static constexpr float RAD2DEG = 180.f / 3.14159265358979323846f;

	[[nodiscard]] static Transform3D Identity() noexcept { return Transform3D{}; }

	/// Matrice locale (translation × rotation × échelle).
	[[nodiscard]] FMatrix4 Matrix() const noexcept { return ComposeTRS(position, rotation, scale); }

	/// Transform correspondant à une matrice (cf. DecomposeTRS et ses limites :
	/// miroir porté sur X, cisaillement approché).
	[[nodiscard]] static Transform3D FromMatrix(const FMatrix4 &m) noexcept;

	/// Angles d'Euler en degrés (inverse exact de SetEulerDegrees).
	[[nodiscard]] FVector3 EulerDegrees() const noexcept;
	void SetEulerDegrees(const FVector3 &degrees) noexcept;

	/// Ce transform appliqué APRÈS `parent` (composition des matrices).
	[[nodiscard]] Transform3D ComposedWith(const Transform3D &parent) const noexcept {
		return FromMatrix(parent.Matrix() * Matrix());
	}

	[[nodiscard]] bool operator==(const Transform3D &o) const noexcept {
		auto same = [](const FVector3 &a, const FVector3 &b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
		return same(position, o.position) && same(scale, o.scale) && rotation.x == o.rotation.x &&
			   rotation.y == o.rotation.y && rotation.z == o.rotation.z && rotation.w == o.rotation.w;
	}
	[[nodiscard]] bool operator!=(const Transform3D &o) const noexcept { return !(*this == o); }
};

struct Transform2D {
	FVector2 position;
	float rotationDegrees = 0.f; ///< autour de Z, sens trigonométrique
	FVector2 scale{1.f, 1.f};

	[[nodiscard]] static Transform2D Identity() noexcept { return Transform2D{}; }

	/// Matrice dans le plan XY (z = 0).
	[[nodiscard]] FMatrix4 Matrix() const noexcept;

	/// Un point du repère local exprimé dans le repère parent.
	[[nodiscard]] FVector2 Apply(const FVector2 &local) const noexcept;

	/// Le transform 3D équivalent (rotation autour de Z, échelle z = 1).
	[[nodiscard]] Transform3D To3D() const noexcept;

	[[nodiscard]] bool operator==(const Transform2D &o) const noexcept {
		return position.x == o.position.x && position.y == o.position.y && rotationDegrees == o.rotationDegrees &&
			   scale.x == o.scale.x && scale.y == o.scale.y;
	}
	[[nodiscard]] bool operator!=(const Transform2D &o) const noexcept { return !(*this == o); }
};

} // namespace math
