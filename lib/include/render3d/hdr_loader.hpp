#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../core/core.hpp"
#include "../sdl3/iostream.hpp"

namespace render3d {

// Décodeur Radiance RGBE (.hdr) écrit à la main — voir le plan (M10) : ni
// SDL3_image (aucun loader HDR dans cette installation, confirmé par
// recherche exhaustive) ni une dépendance externe supplémentaire, juste le
// format lui-même (en-tête ASCII + scanlines RGBE, éventuellement RLE) qui
// est simple à décoder sans bibliothèque. Restriction assumée : seule
// l'orientation standard "-Y H +X W" (la quasi-totalité des fichiers .hdr
// réels, y compris tous ceux utilisés pour des environment maps équirectangulaires)
// est supportée ; l'ancien format RLE (pixel R=G=B=1 signalant une répétition)
// n'est pas supporté, seuls le format "plat" et le nouveau RLE 4-canaux le sont
// (couvre la quasi-totalité des fichiers produits par les encodeurs modernes).
struct HdrImage {
	std::vector<float> pixels; // RGB, 3 float/pixel, ligne par ligne, haut vers bas
	int width = 0;
	int height = 0;
};

namespace detail {

[[nodiscard]] bool HdrFindLineEnd(const std::vector<uint8_t> &bytes, size_t &pos, size_t &lineStart,
										 size_t &lineLen);

// Décode un octet RGBE (r,g,b,e partagé) en RGB linéaire — e=0 signifie noir
// pur (aucun exposant valide), formule standard Radiance sinon.
void HdrDecodeRgbe(uint8_t r, uint8_t g, uint8_t b, uint8_t e, float &outR, float &outG, float &outB);

} // namespace detail

[[nodiscard]] Result<HdrImage, String> LoadHdrEquirectangular(const String &path);

} // namespace render3d
