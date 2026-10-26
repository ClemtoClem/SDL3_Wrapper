#pragma once
/**
 * ui::ShaderEffect — post-traitement GPU du sous-arbre d'un widget.
 *
 * Chaque image, avant le dessin 2D, le sous-arbre du widget (lui et sa
 * descendance) est dessiné dans une texture cible du renderer, relu, envoyé
 * au device GPU du `render3d::Canvas` (`Ui::Initialize`), passé dans un
 * pipeline de fragment dédié (triangle plein écran), puis relu et affiché À
 * LA PLACE du dessin normal du sous-arbre. Sans Canvas, ou à tout échec GPU,
 * le sous-arbre est dessiné normalement (ou garde la dernière image valide).
 *
 * Coût : deux allers-retours CPU↔GPU par widget et par image — à réserver
 * à quelques widgets.
 *
 * @code{.cpp}
 * ui::Panel().With(ui::TintShift(ui::Rgba(255, 120, 80), 0.6f)).Children({...});
 * ui::SetShaderEffect(card, ui::GlowEffect(ui::Rgba(80, 160, 255), 0.25f));
 * @endcode
 */
#include "builder.hpp"

namespace ui {

enum class ShaderEffectKind : uint8_t {
	/// rgb = mix(src, src * color, param)
	TINT_SHIFT,
	/// halo radial : rgb = mix(src, color, smoothstep(param, 0.5, |uv - centre|))
	GLOW,
};

/// Brique : effet appliqué au sous-arbre de l'entité.
struct ShaderEffect {
	ShaderEffectKind kind = ShaderEffectKind::TINT_SHIFT;
	Color color{1.f, 1.f, 1.f, 1.f};
	float param = 0.5f;
	bool enabled = true;
};

[[nodiscard]] inline ShaderEffect TintShift(Color color, float amount) {
	return ShaderEffect{ShaderEffectKind::TINT_SHIFT, color, amount, true};
}
[[nodiscard]] inline ShaderEffect GlowEffect(Color color, float innerRadius) {
	return ShaderEffect{ShaderEffectKind::GLOW, color, innerRadius, true};
}

/// Pose (ou remplace) l'effet d'un widget ; NONE le retire.
void SetShaderEffect(Widget widget, Option<ShaderEffect> effect);

/// Capture, pipeline GPU et affichage des effets (enregistrée par ui::Ui).
void RegisterShaderEffectExtension(Context &ctx);

namespace detail {
extern const char *const FULLSCREEN_TRIANGLE_VERT;
extern const char *const TINT_SHIFT_FRAG;
extern const char *const GLOW_FRAG;
} // namespace detail

} // namespace ui
