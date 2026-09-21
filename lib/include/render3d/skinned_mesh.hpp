#pragma once
#include <memory>
#include <vector>

#include "canvas.hpp"
#include "material.hpp"
#include "object3d.hpp"
#include "skinned_geometry.hpp"

namespace render3d {

/// Un os du squelette (three.js Bone, M17) — un Object3D utilisé comme
/// articulation nommée dans la hiérarchie n'a besoin d'aucun champ
/// supplémentaire (position/rotation/scale/hiérarchie/WorldMatrix() sont
/// déjà tout ce qu'un os requiert, voir object3d.hpp) : cette classe existe
/// uniquement pour documenter l'intention à l'appel (`std::make_unique<Bone>()`
/// plutôt que `std::make_unique<Object3D>()`), pas pour ajouter du comportement.
class Bone : public Object3D {};

/// Maillage skinné (three.js SkinnedMesh, M17 du plan) : une SkinnedGeometry
/// + un squelette de Bone (racine possédée, liste plate référencée par
/// SkinnedVertex3D::boneIndices) + les matrices d'inverse bind (capturées une
/// fois à la construction, via Bone::WorldMatrix() à la pose de liaison).
/// Chaque frame, OnDraw() recalcule les matrices de skinning courantes
/// (bone.WorldMatrix() * inverseBind, PAS de cache — même politique
/// qu'Object3D::WorldMatrix()) et les envoie à Canvas::DrawSkinnedMesh().
///
/// Portée délibérément limitée cette phase (voir le plan) : pas de lecture
/// d'animation par clips/keyframes — seule la pose manuelle via
/// Bone::SetRotation()/SetPosition() (héritées d'Object3D) est supportée,
/// suffisante pour vérifier que le skinning GPU déforme réellement le
/// maillage sans avoir besoin d'un système d'animation complet.
class SkinnedMesh : public Object3D {
	SkinnedGeometry m_geometry;
	Material m_material;
	// Possédé via Object3D::Add() (donc dans Children(), pas un unique_ptr
	// séparé) : le squelette suit la transform du SkinnedMesh lui-même,
	// comme le reste de la hiérarchie Object3D.
	Bone *m_rootBone;
	std::vector<Bone *> m_bones; // ordre = index référencé par SkinnedVertex3D::boneIndices
	std::vector<math::FMatrix4> m_inverseBindMatrices;

public:
	/// `bones` doit lister CHAQUE os référencé par les index de
	/// `geometry` (y compris `*rootBone` lui-même s'il est skinné), dans
	/// l'ordre attendu par SkinnedVertex3D::boneIndices — capturé comme pose
	/// de liaison (bind pose) immédiatement, avant toute pose manuelle.
	SkinnedMesh(SkinnedGeometry geometry, Material material, std::unique_ptr<Bone> rootBone,
			   std::vector<Bone *> bones)
		: m_geometry(std::move(geometry)), m_material(std::move(material)),
		  m_rootBone(static_cast<Bone *>(&Add(std::move(rootBone)))), m_bones(std::move(bones)) {
		m_inverseBindMatrices.reserve(m_bones.size());
		for (Bone *bone : m_bones)
			m_inverseBindMatrices.push_back(bone->WorldMatrix().Inverse());
	}

	[[nodiscard]] Bone &RootBone() noexcept { return *m_rootBone; }
	[[nodiscard]] const std::vector<Bone *> &Bones() const noexcept { return m_bones; }
	[[nodiscard]] SkinnedGeometry &Geometry() noexcept { return m_geometry; }

	/// Matrices de skinning courantes — recalculées à chaque appel (voir la
	/// note de classe ci-dessus).
	[[nodiscard]] std::vector<math::FMatrix4> ComputeSkinMatrices() const {
		std::vector<math::FMatrix4> result;
		result.reserve(m_bones.size());
		for (size_t i = 0; i < m_bones.size(); ++i)
			result.push_back(m_bones[i]->WorldMatrix() * m_inverseBindMatrices[i]);
		return result;
	}

	void OnDraw(Canvas &canvas) override {
		std::vector<math::FMatrix4> skinMatrices = ComputeSkinMatrices();
		canvas.DrawSkinnedMesh(m_geometry, skinMatrices, m_material);
	}
};

} // namespace render3d
