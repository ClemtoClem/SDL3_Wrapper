#pragma once
/**
 * Opérations de modélisation sur un Model — l'équivalent des outils de
 * SketchUp (Pousser/Tirer, Décalage, Suivez-moi, Déplacer, Rotation,
 * Échelle, Gomme, Pot de peinture, Adoucir les arêtes) et des aides de
 * construction d'openskp (`extrude`, `revolve`).
 *
 * Toutes rendent Result<…, String> : le message d'erreur est destiné à
 * l'utilisateur (barre d'état de l'éditeur, console de script).
 */
#include <vector>

#include "model.hpp"

namespace modeling {

struct PushPullOptions {
	/// Garde la face de départ (Ctrl dans SketchUp) : crée un « étage ».
	bool keepOriginal = false;
};

/// Pousse/tire une face le long de sa normale de `distance` mètres
/// (négatif = vers l'arrière). Rend la face déplacée/créée.
Result<FaceId, String> PushPull(Model &model, FaceId face, float distance, const PushPullOptions &options = {});

/// Contour décalé d'une face (sans modifier le modèle) : aperçu de l'outil
/// Décalage. Même convention de signe que Offset.
Result<std::vector<Vec3>, String> OffsetOutline(const Model &model, FaceId face, float distance);

/// Décale le contour d'une face vers l'intérieur (`distance` > 0) ou
/// l'extérieur (< 0). Rend la nouvelle face (intérieure, ou l'anneau
/// extérieur).
Result<FaceId, String> Offset(Model &model, FaceId face, float distance);

/// Balaie le profil (une face) le long d'un chemin (Suivez-moi). Un chemin
/// fermé (dernier point = premier) produit un anneau sans extrémités.
/// Rend la face d'extrémité (ou le profil pour un chemin fermé).
Result<FaceId, String> FollowMe(Model &model, FaceId profile, const std::vector<Vec3> &path);

/// Prisme : le contour plan `outline` balayé le long de `vector`
/// (openskp `extrude`). Rend la face d'arrivée.
Result<FaceId, String> Extrude(Model &model, const std::vector<Vec3> &outline, const Vec3 &vector,
							   uint32_t material = INVALID_ID);

struct RevolveOptions {
	int segments = 32;
	/// Profil fermé (section d'anneau) : pas de faces de fermeture.
	bool closed = false;
	uint32_t material = INVALID_ID;
	/// Angle balayé (radians) ; 2π = tour complet.
	float angle = 2.f * math::PI;
};

/// Solide de révolution autour de l'axe (origin, axis) d'après un profil
/// (rayon, hauteur) du bas vers le haut (openskp `revolve`). Les faces
/// latérales sont adoucies et lissées.
Result<std::vector<FaceId>, String> Revolve(Model &model, const Vec3 &origin, const Vec3 &axis,
											const std::vector<math::FVector2> &profile, const RevolveOptions &options = {});

/// Ordonne des arêtes formant une chaîne (sans embranchement) en un chemin
/// de points, commençant par l'extrémité la plus proche de `near` (pour
/// Suivez-moi). Un chemin fermé se termine par son premier point.
Result<std::vector<Vec3>, String> ChainEdges(const Model &model, const std::vector<EdgeId> &edges, const Vec3 &near);

/// Copie des faces données (avec leurs arêtes, matériaux, calques) dans un
/// nouveau modèle — base des copies (Déplacer + Ctrl).
[[nodiscard]] Model ExtractFaces(const Model &model, const std::vector<FaceId> &faces);

/// Sommets utilisés par des faces et des arêtes (sans doublon).
[[nodiscard]] std::vector<VertexId> VerticesOf(const Model &model, const std::vector<FaceId> &faces,
											   const std::vector<EdgeId> &edges = {});

/// Déplace, tourne ou met à l'échelle une sélection, puis soude les sommets
/// devenus confondus (comme le font les outils de SketchUp).
void Move(Model &model, const std::vector<VertexId> &vertices, const Vec3 &delta);
void Rotate(Model &model, const std::vector<VertexId> &vertices, const Vec3 &center, const Vec3 &axis, float angle);
void Scale(Model &model, const std::vector<VertexId> &vertices, const Vec3 &center, const Vec3 &factors);

/// Gomme : supprime des arêtes (et les faces qui s'y appuient) ou des faces.
void Erase(Model &model, const std::vector<EdgeId> &edges, const std::vector<FaceId> &faces = {});

/// Pot de peinture : matériau d'une face (INVALID_ID = matériau par défaut).
void Paint(Model &model, FaceId face, uint32_t material);

/// Adoucit et lisse les arêtes entre deux faces dont les normales font un
/// angle inférieur à `maxAngle` (radians) — « Adoucir/Lisser les arêtes ».
/// Limité aux faces données (toutes si vide). Rend le nombre d'arêtes adoucies.
size_t SoftenEdges(Model &model, float maxAngle, const std::vector<FaceId> &faces = {});

} // namespace modeling
