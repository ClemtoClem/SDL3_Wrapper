// Définitions de render3d/material.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/material.hpp"

namespace render3d {

// ── Material ─────────────────────────────────────────────────────────────────

Material Material::Plastic(sdl3::Color color) noexcept {
	Material m;
	m.baseColor = color;
	m.roughness = 0.4f;
	return m;
}

Material Material::Metal(sdl3::Color color) noexcept {
	Material m;
	m.baseColor = color;
	m.roughness = 0.2f;
	m.metallic = 1.f;
	return m;
}

Material Material::Wood(sdl3::Color color) noexcept {
	Material m;
	m.baseColor = color;
	m.roughness = 0.8f;
	return m;
}

Material Material::Unlit(sdl3::Color color) noexcept {
	Material m;
	m.baseColor = color;
	m.lit = false;
	return m;
}

Material Material::Pbr(sdl3::Color color, float metallic, float roughness) noexcept {
	Material m;
	m.baseColor = color;
	m.metallic = metallic;
	m.roughness = roughness;
	m.pbr = true;
	return m;
}

} // namespace render3d
