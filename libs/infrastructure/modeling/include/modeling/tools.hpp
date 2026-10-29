#pragma once
/**
 * Outils de modélisation interactifs à la SketchUp, sous forme de machines
 * à états PURES : ils reçoivent le point inféré sous le curseur (survol,
 * clic), la saisie au clavier des cotes (« boîte de mesures » de SketchUp :
 * taper 2,5 puis Entrée après avoir orienté une ligne), et décrivent leur
 * aperçu (segments, contour, consigne, mesure) que l'interface dessine.
 * Aucune dépendance à une fenêtre ni à une caméra : chaque outil se teste
 * en simulant des clics.
 *
 * Raccourcis de SketchUp conservés (ToolShortcut) : Espace sélection,
 * L ligne, R rectangle, C cercle, P pousser/tirer, F décalage, M déplacer,
 * Q rotation, S échelle, E gomme, B peinture, T mètre.
 */
#include <memory>
#include <vector>

#include "inference.hpp"
#include "model.hpp"

namespace modeling {

enum class ToolKind : uint8_t {
	SELECT,
	LINE,
	RECTANGLE,
	CIRCLE,
	POLYGON,
	PUSH_PULL,
	OFFSET,
	FOLLOW_ME,
	MOVE,
	ROTATE,
	SCALE,
	ERASER,
	PAINT,
	TAPE,
};

inline constexpr ToolKind ALL_TOOLS[] = {
	ToolKind::SELECT,  ToolKind::LINE,      ToolKind::RECTANGLE, ToolKind::CIRCLE, ToolKind::POLYGON,
	ToolKind::PUSH_PULL, ToolKind::OFFSET, ToolKind::FOLLOW_ME, ToolKind::MOVE,   ToolKind::ROTATE,
	ToolKind::SCALE,   ToolKind::ERASER,    ToolKind::PAINT,     ToolKind::TAPE,
};

[[nodiscard]] const char *ToolName(ToolKind kind) noexcept;
/// Touche de raccourci (minuscule) ; '\0' si aucune.
[[nodiscard]] char ToolShortcut(ToolKind kind) noexcept;
[[nodiscard]] Option<ToolKind> ToolFromShortcut(char key) noexcept;

/// Sélection de faces et d'arêtes du modèle en cours d'édition.
struct Selection {
	std::vector<FaceId> faces;
	std::vector<EdgeId> edges;

	[[nodiscard]] bool IsEmpty() const noexcept { return faces.empty() && edges.empty(); }
	void Clear() {
		faces.clear();
		edges.clear();
	}
	[[nodiscard]] bool HasFace(FaceId f) const;
	[[nodiscard]] bool HasEdge(EdgeId e) const;
	void ToggleFace(FaceId f);
	void ToggleEdge(EdgeId e);
	/// Retire ce qui n'existe plus (après une opération).
	void Prune(const Model &model);
	[[nodiscard]] std::vector<VertexId> Vertices(const Model &model) const;
};

struct ToolEvent {
	Inference at;
	math::FRay ray; ///< rayon de la souris, repère du modèle
	bool shift = false;
	bool ctrl = false;
};

struct ToolContext {
	Model &model;
	Selection &selection;
	uint32_t material = INVALID_ID; ///< matériau courant (peinture, nouvelles faces)
	int circleSegments = 24;
	int polygonSides = 6;
};

struct ToolPreview {
	std::vector<std::pair<Vec3, Vec3>> lines;  ///< traits d'aperçu
	std::vector<std::pair<Vec3, Vec3>> guides; ///< pointillés (mètre, référence de rotation)
	std::vector<Vec3> polygon;                 ///< contour mis en évidence
	String hint;                               ///< consigne pour l'étape en cours
	String measure;                            ///< valeur courante (« Longueur 1,25 m »)
};

struct ToolResult {
	bool changed = false;       ///< le modèle a été modifié (à enregistrer, annulable)
	Option<String> error = NONE;
	String message;             ///< retour pour la barre d'état
};

class Tool {
public:
	explicit Tool(ToolKind kind) noexcept : m_kind(kind) {}
	virtual ~Tool() = default;

	[[nodiscard]] ToolKind Kind() const noexcept { return m_kind; }
	[[nodiscard]] const char *Name() const noexcept { return ToolName(m_kind); }

	/// Abandonne l'opération en cours (Échap).
	virtual void Reset() = 0;
	/// Une opération est-elle en cours (au moins un clic posé) ?
	[[nodiscard]] virtual bool IsActive() const = 0;
	virtual void Hover(ToolContext &ctx, const ToolEvent &event) = 0;
	virtual ToolResult Click(ToolContext &ctx, const ToolEvent &event) = 0;
	/// Cotes tapées au clavier puis Entrée (longueur, « largeur;hauteur »,
	/// rayon, distance, angle en degrés, facteur…).
	virtual ToolResult Type(ToolContext &ctx, const std::vector<float> &values);
	[[nodiscard]] virtual ToolPreview Preview(const ToolContext &ctx) const = 0;
	/// Point d'ancrage pour les inférences d'axes (point précédent).
	[[nodiscard]] virtual Option<Vec3> Anchor() const { return NONE; }

private:
	ToolKind m_kind;
};

[[nodiscard]] std::unique_ptr<Tool> MakeTool(ToolKind kind);

/// Lit une saisie de cotes : « 2,5 » ou « 2.5 », plusieurs valeurs séparées
/// par « ; », espace ou « x », unités m/cm/mm et ° acceptées (cm et mm
/// convertis en mètres). Vide si la saisie est invalide.
[[nodiscard]] std::vector<float> ParseMeasurement(const String &text);

/// « 1,25 m », « 45° »… (format français).
[[nodiscard]] String FormatLength(float meters);
[[nodiscard]] String FormatAngle(float radians);

} // namespace modeling
