#pragma once
#include "resource.hpp"
#include <vector>

#include "../sdl3/iostream.hpp"

namespace resources {

/// Ressource générique : charge le contenu BRUT d'un fichier en mémoire via
/// `sdl3::readFile()` — indépendante d'un renderer/fenêtre (testable sans
/// contexte graphique). Démonstration minimale que le module fonctionne
/// réellement de bout en bout (aucune sous-classe concrète de `Resource`
/// n'existait nulle part avant ce chantier) ; sert aussi de base pour des
/// ressources plus spécialisées (texture, police...) qui décoderaient ces
/// octets bruts après coup.
class BytesResource : public Resource {
public:
	explicit BytesResource(String path) : m_path(std::move(path)) {}

	[[nodiscard]] StringView Path() const noexcept override { return m_path.View(); }
	[[nodiscard]] const std::vector<uint8_t> &Bytes() const noexcept { return m_bytes; }

protected:
	Result<bool, String> DoLoad() override {
		auto result = sdl3::ReadFile(m_path);
		if (!result.IsOk())
			return Err(String(result.Error()));
		m_bytes = std::move(result.Value());
		return Ok(true);
	}
	void DoUnload() override { m_bytes.clear(); }

private:
	String m_path;
	std::vector<uint8_t> m_bytes;
};

} // namespace resources
