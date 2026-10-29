#pragma once
/**
 * ModelingSession — édition d'un nœud MESH à la manière de SketchUp, dans la
 * vue 3D de l'éditeur.
 *
 * La session relie les outils PURS de modeling:: (tools.hpp : ligne,
 * rectangle, pousser/tirer…) à la vue : elle convertit la souris en rayon
 * dans le repère du MODÈLE (matrice monde du nœud), calcule l'inférence sous
 * le curseur (extrémité, milieu, axes… — projection par la caméra de la
 * vue), tient la « boîte de mesures » (cotes tapées au clavier puis Entrée),
 * l'annulation propre au modèle, et décrit ce que l'interface doit dessiner
 * (Overlay en pixels, arêtes du modèle en 3D via Runtime::SetModelingLines).
 *
 * Aucune dépendance à l'interface ni à SDL : les tests pilotent la session
 * avec des coordonnées de pixels, comme le ferait la souris.
 */
#include <functional>
#include <memory>
#include <vector>

#include <core/core.hpp>
#include <modeling/inference.hpp>
#include <modeling/tools.hpp>
#include <sandbox/engine/runtime.hpp>

namespace sandbox {

class ModelingSession {
public:
	explicit ModelingSession(Runtime &runtime) : m_rt(runtime) {}
	~ModelingSession();
	ModelingSession(const ModelingSession &) = delete;
	ModelingSession &operator=(const ModelingSession &) = delete;

	// ── Cycle de vie ─────────────────────────────────────────────────────────

	/// Édite un nœud : un nœud MESH directement, une forme primitive après
	/// conversion en maillage modélisé (Runtime::ConvertToMesh).
	Result<bool, String> Begin(scene::NodeId id);
	void End();
	/// Vrai tant que le nœud édité existe encore.
	[[nodiscard]] bool IsActive() const;
	[[nodiscard]] scene::NodeId Node() const noexcept { return m_node; }
	[[nodiscard]] const String &Source() const noexcept { return m_source; }
	/// Modèle édité (nullptr hors session).
	[[nodiscard]] modeling::Model *Model() const;

	// ── Outils ───────────────────────────────────────────────────────────────

	void SetTool(modeling::ToolKind kind);
	[[nodiscard]] modeling::ToolKind CurrentTool() const noexcept { return m_toolKind; }

	uint32_t material = modeling::INVALID_ID; ///< matériau de la peinture
	int circleSegments = 24;
	int polygonSides = 6;
	float gridStep = 0.f; ///< accroche au sol (0 = libre)

	[[nodiscard]] modeling::Selection &Selection() noexcept { return m_selection; }

	// ── Esquisse (espace de travail 2D pendant la modélisation) ────────────
	//
	// Un plan de travail (repère du MODÈLE) : plan d'axes ou face d'un nœud.
	// Les outils y posent tous leurs points (cf. InferenceSettings::workPlane)
	// et une grille le matérialise.

	void SetSketchPlane(Option<modeling::InferenceSettings::WorkPlane> plane);
	/// Même chose d'après un point et une normale MONDE (face d'un autre nœud).
	bool SetSketchPlaneWorld(const math::FVector3 &point, const math::FVector3 &normal);
	/// Plan = face du modèle sous le curseur ; faux s'il n'y en a pas.
	bool PickSketchFace(math::FVector2 cursor);
	[[nodiscard]] const Option<modeling::InferenceSettings::WorkPlane> &SketchPlane() const noexcept { return m_sketch; }
	[[nodiscard]] bool IsSketching() const noexcept { return m_sketch.IsSome(); }
	/// Plan d'esquisse en repère MONDE : (point, normale unitaire).
	[[nodiscard]] Option<std::pair<math::FVector3, math::FVector3>> SketchPlaneWorld() const;

	// ── Souris et clavier (pixels relatifs au coin haut-gauche de la vue) ───

	void SetViewportSize(float width, float height) noexcept;
	void PointerMove(math::FVector2 cursor);
	/// Clic gauche ; vrai si la session l'a utilisé.
	bool Click(math::FVector2 cursor, bool shift = false, bool ctrl = false);
	/// Un caractère tapé : chiffre ou unité vers la boîte de mesures, sinon
	/// raccourci d'outil (L, R, C, P…) quand la boîte est vide.
	bool TypeChar(char c);
	bool Backspace();
	/// Entrée : applique la cote tapée à l'outil en cours.
	bool Commit();
	/// Échap : abandonne l'opération, puis vide la saisie ; faux s'il n'y
	/// avait rien à abandonner (l'éditeur quitte alors la session).
	bool Escape();
	bool DeleteSelection();
	bool Undo();
	bool Redo();
	[[nodiscard]] bool CanUndo() const noexcept { return !m_undo.empty(); }
	[[nodiscard]] bool CanRedo() const noexcept { return !m_redo.empty(); }

	/// Opération décidée par l'interface (inspecteur : adoucir, primitive…),
	/// annulable comme un clic d'outil. `op` rend vrai s'il a modifié le modèle.
	bool Apply(const String &label, const std::function<bool(modeling::Model &)> &op);

	[[nodiscard]] const String &Typed() const noexcept { return m_typed; }
	[[nodiscard]] const String &Status() const noexcept { return m_status; }
	[[nodiscard]] const modeling::Inference &Hovered() const noexcept { return m_hover; }

	// ── Dessin ───────────────────────────────────────────────────────────────

	/// Ce que l'interface dessine par-dessus la vue, en pixels.
	struct Overlay {
		struct Segment {
			math::FVector2 a, b;
			sdl3::Color color;
			bool dashed = false;
		};
		std::vector<Segment> segments;
		std::vector<math::FVector2> polygon; ///< contour mis en évidence
		Option<math::FVector2> marker = NONE; ///< point inféré
		sdl3::Color markerColor{0, 0, 0, 255};
		String label;   ///< « Extrémité », « Sur l'axe rouge »…
		String hint;    ///< consigne de l'outil
		String measure; ///< « Longueur 1,25 m » ou la saisie en cours
	};
	[[nodiscard]] Overlay BuildOverlay() const;

	/// Republie les arêtes 3D (après un changement de caméra, de sélection…).
	void RefreshLines();
	/// Boîte englobante du modèle dans le repère MONDE (NONE s'il est vide).
	[[nodiscard]] Option<math::FAABB> WorldBounds() const;
	/// Pixel d'un point du modèle (repère du modèle) ; NONE derrière la caméra.
	[[nodiscard]] Option<math::FVector2> ScreenOf(const modeling::Vec3 &local) const;

private:
	[[nodiscard]] Option<math::FMatrix4> WorldMatrix() const;
	[[nodiscard]] Option<math::FVector2> Project(const math::FMatrix4 &world, const modeling::Vec3 &p) const;
	[[nodiscard]] modeling::ToolEvent EventAt(math::FVector2 cursor, bool shift, bool ctrl);
	[[nodiscard]] modeling::ToolContext Context(modeling::Model &model);
	void Report(const modeling::ToolResult &result, String undoSnapshot);
	void ModelChanged();

	Runtime &m_rt;
	scene::NodeId m_node{};
	String m_source;
	modeling::ToolKind m_toolKind = modeling::ToolKind::SELECT;
	std::unique_ptr<modeling::Tool> m_tool;
	modeling::Selection m_selection;
	modeling::Inference m_hover;
	Option<modeling::InferenceSettings::WorkPlane> m_sketch = NONE;
	std::vector<String> m_undo, m_redo;
	String m_typed, m_status;
	math::FVector2 m_cursor{0.f, 0.f};
	float m_width = 1.f, m_height = 1.f;
};

} // namespace sandbox
