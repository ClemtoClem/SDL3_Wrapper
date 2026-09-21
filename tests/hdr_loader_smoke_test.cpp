// Smoke test : décodeur Radiance RGBE écrit à la main (lib/render3d/hdr_loader.hpp)
// — M10 du plan. Aucun fichier .hdr réel : un petit fichier synthétique est
// généré à l'exécution (en-tête ASCII + 2 scanlines de 10 pixels, l'une
// "plate", l'autre en nouveau RLE 4-canaux mêlant dump et run) puis relu et
// comparé aux valeurs RGB attendues, calculées à la main via la formule
// Radiance standard (voir le commentaire de detail::HdrDecodeRgbe).
#define USE_TEST
#include "core/test.hpp"
#include "render3d/hdr_loader.hpp"
#include "sdl3/sdl3.hpp"
#include <cmath>
#include <vector>

using namespace sdl3;
using namespace render3d;

namespace {

void AppendBytes(std::vector<uint8_t> &buf, std::initializer_list<uint8_t> bytes) {
    buf.insert(buf.end(), bytes.begin(), bytes.end());
}

void AppendString(std::vector<uint8_t> &buf, const char *s) {
    while (*s)
        buf.push_back(uint8_t(*s++));
}

} // namespace

TEST(HdrLoaderSmokeTest, DecodesFlatAndRleScanlines) {
    std::vector<uint8_t> file;
    AppendString(file, "#?RADIANCE\n");
    AppendString(file, "FORMAT=32-bit_rle_rgbe\n");
    AppendString(file, "\n"); // ligne vide : fin d'en-tête
    AppendString(file, "-Y 2 +X 10\n");

    // Ligne 0 : "plate" — 10 pixels identiques (128,64,32,130).
    // scale = 2^(130-136) = 2^-6 = 0.015625 -> R=2.0, G=1.0, B=0.5
    for (int x = 0; x < 10; ++x)
        AppendBytes(file, {128, 64, 32, 130});

    // Ligne 1 : nouveau RLE (4 plans R,G,B,E) — R en "dump" (10 valeurs
    // littérales distinctes), G/B/E en "run" (constants sur les 10 pixels).
    AppendBytes(file, {2, 2, 0, 10}); // marqueur nouveau RLE, largeur=10
    // Plan R : dump de 10 octets 10,20,...,100.
    file.push_back(10);
    for (int x = 0; x < 10; ++x)
        file.push_back(uint8_t(10 * (x + 1)));
    // Plan G : run de 10 x la valeur 60.
    AppendBytes(file, {uint8_t(128 + 10), 60});
    // Plan B : run de 10 x la valeur 40.
    AppendBytes(file, {uint8_t(128 + 10), 40});
    // Plan E : run de 10 x la valeur 128 -> scale = 2^(128-136) = 2^-8 = 0.00390625
    AppendBytes(file, {uint8_t(128 + 10), 128});

    String path("build/tests/synthetic_test.hdr");
    ASSERT_TRUE(WriteFile(path, file.data(), file.size()));

    auto result = LoadHdrEquirectangular(path);
    ASSERT_TRUE(result.IsOk());
    HdrImage image = std::move(result.Value());
    ASSERT_TRUE(image.width == 10);
    ASSERT_TRUE(image.height == 2);
    ASSERT_TRUE(image.pixels.size() == size_t(10 * 2 * 3));

    // Ligne 0 (plate) : tous les pixels identiques.
    for (int x = 0; x < 10; ++x) {
        size_t idx = (size_t(0) * 10 + size_t(x)) * 3;
        EXPECT_TRUE(std::abs(image.pixels[idx + 0] - 2.0f) < 1e-4f);
        EXPECT_TRUE(std::abs(image.pixels[idx + 1] - 1.0f) < 1e-4f);
        EXPECT_TRUE(std::abs(image.pixels[idx + 2] - 0.5f) < 1e-4f);
    }

    // Ligne 1 (RLE) : G/B constants, R varie par pixel (vérifie que le dump
    // décode bien un octet distinct par pixel, pas une seule valeur répétée).
    float scale1 = std::pow(2.0f, 128.f - 136.f); // 0.00390625
    for (int x = 0; x < 10; ++x) {
        size_t idx = (size_t(1) * 10 + size_t(x)) * 3;
        float expectedR = float(10 * (x + 1)) * scale1;
        EXPECT_TRUE(std::abs(image.pixels[idx + 0] - expectedR) < 1e-5f);
        EXPECT_TRUE(std::abs(image.pixels[idx + 1] - 60.f * scale1) < 1e-5f);
        EXPECT_TRUE(std::abs(image.pixels[idx + 2] - 40.f * scale1) < 1e-5f);
    }
}

TEST(HdrLoaderSmokeTest, RejectsMissingFile) {
    auto result = LoadHdrEquirectangular(String("build/tests/does_not_exist.hdr"));
    EXPECT_TRUE(result.IsError());
}

TEST(HdrLoaderSmokeTest, LoadsRealAssetUsedByShowcaseExample) {
    // Ciel procédural généré une fois (voir assets/textures/equirectangularmaps/) —
    // pas de vrai fichier .hdr trouvable dans ce dépôt/environnement (voir le
    // plan M19), régénéré par un petit script Python séparé plutôt qu'une
    // dépendance réseau. Ce test protège contre une régression du fichier
    // OU du loader sur le vrai asset utilisé par examples/render3d_showcase.cpp.
    auto result = LoadHdrEquirectangular(String("assets/textures/equirectangularmaps/procedural_sky.hdr"));
    ASSERT_TRUE(result.IsOk());
    HdrImage image = std::move(result.Value());
    EXPECT_TRUE(image.width == 256);
    EXPECT_TRUE(image.height == 128);
    EXPECT_TRUE(image.pixels.size() == size_t(256 * 128 * 3));
    // Ligne du haut (zénith) : bleu dominant, aucune valeur négative/NaN.
    float r = image.pixels[0], g = image.pixels[1], b = image.pixels[2];
    EXPECT_TRUE(r >= 0.f && g >= 0.f && b >= 0.f);
    EXPECT_TRUE(b > r);
}

int main() { return RUN_ALL_TESTS(); }
