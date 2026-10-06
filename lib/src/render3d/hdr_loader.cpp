// Définitions de render3d/hdr_loader.hpp
#include "render3d/hdr_loader.hpp"

namespace render3d {

namespace detail {

bool HdrFindLineEnd(const std::vector<uint8_t> &bytes, size_t &pos, size_t &lineStart, size_t &lineLen) {
	if (pos >= bytes.size())
		return false;
	lineStart = pos;
	while (pos < bytes.size() && bytes[pos] != '\n')
		++pos;
	lineLen = pos - lineStart;
	if (pos < bytes.size())
		++pos; // saute le '\n'
	return true;
}

void HdrDecodeRgbe(uint8_t r, uint8_t g, uint8_t b, uint8_t e, float &outR, float &outG, float &outB) {
	if (e == 0) {
		outR = outG = outB = 0.f;
		return;
	}
	float scale = std::pow(2.f, float(int(e) - (128 + 8)));
	outR = float(r) * scale;
	outG = float(g) * scale;
	outB = float(b) * scale;
}

} // namespace detail

Result<HdrImage, String> LoadHdrEquirectangular(const String &path) {
	auto fileResult = sdl3::ReadFile(path);
	if (!fileResult)
		return Err(String(fileResult.Error()));
	const std::vector<uint8_t> &bytes = fileResult.Value();

	size_t pos = 0;
	// En-tête ASCII : lignes clé=valeur jusqu'à une ligne vide.
	size_t lineStart = 0, lineLen = 0;
	bool sawMagic = false;
	while (detail::HdrFindLineEnd(bytes, pos, lineStart, lineLen)) {
		if (lineLen == 0)
			break; // ligne vide -> fin de l'en-tête
		if (!sawMagic && lineLen >= 2 && bytes[lineStart] == '#' && bytes[lineStart + 1] == '?')
			sawMagic = true;
	}
	if (!sawMagic)
		return Err(String("LoadHdrEquirectangular: en-tête Radiance introuvable (#?...)"));

	// Ligne de résolution : "-Y <h> +X <w>" — seule orientation supportée.
	if (!detail::HdrFindLineEnd(bytes, pos, lineStart, lineLen))
		return Err(String("LoadHdrEquirectangular: fichier tronqué (ligne de résolution manquante)"));
	int width = 0, height = 0;
	{
		String resLine(reinterpret_cast<const char *>(bytes.data() + lineStart), lineLen);
		// Recherche manuelle de "-Y" puis "+X" suivis d'un entier — évite de
		// dépendre de sscanf sur une String non null-terminée à cette longueur.
		const char *cstr = resLine.CStr();
		const char *yTag = std::strstr(cstr, "-Y");
		const char *xTag = std::strstr(cstr, "+X");
		if (!yTag || !xTag)
			return Err(String("LoadHdrEquirectangular: orientation non supportée (attendu \"-Y H +X W\")"));
		height = std::atoi(yTag + 2);
		width = std::atoi(xTag + 2);
	}
	if (width <= 0 || height <= 0)
		return Err(String("LoadHdrEquirectangular: résolution invalide"));

	HdrImage image;
	image.width = width;
	image.height = height;
	image.pixels.resize(size_t(width) * size_t(height) * 3);

	std::vector<uint8_t> scanline(size_t(width) * 4);
	for (int y = 0; y < height; ++y) {
		if (pos + 4 > bytes.size())
			return Err(String("LoadHdrEquirectangular: données de scanline tronquées"));

		bool isNewRle = width >= 8 && width < 0x7fff && bytes[pos] == 2 && bytes[pos + 1] == 2 &&
						(bytes[pos + 2] & 0x80) == 0 && (int(bytes[pos + 2]) << 8 | int(bytes[pos + 3])) == width;

		if (isNewRle) {
			pos += 4;
			for (int channel = 0; channel < 4; ++channel) {
				int x = 0;
				while (x < width) {
					if (pos >= bytes.size())
						return Err(String("LoadHdrEquirectangular: RLE tronqué"));
					uint8_t count = bytes[pos++];
					if (count > 128) {
						// run : (count-128) répétitions de l'octet suivant
						int runLen = int(count) - 128;
						if (pos >= bytes.size() || x + runLen > width)
							return Err(String("LoadHdrEquirectangular: run RLE invalide"));
						uint8_t value = bytes[pos++];
						for (int i = 0; i < runLen; ++i)
							scanline[size_t(x + i) * 4 + size_t(channel)] = value;
						x += runLen;
					} else {
						// dump : `count` octets littéraux
						if (pos + count > bytes.size() || x + count > width)
							return Err(String("LoadHdrEquirectangular: dump RLE invalide"));
						for (int i = 0; i < int(count); ++i)
							scanline[size_t(x + i) * 4 + size_t(channel)] = bytes[pos++];
						x += int(count);
					}
				}
			}
		} else {
			// Scanline "plate" : W pixels de 4 octets RGBE consécutifs.
			size_t need = size_t(width) * 4;
			if (pos + need > bytes.size())
				return Err(String("LoadHdrEquirectangular: scanline plate tronquée"));
			std::memcpy(scanline.data(), bytes.data() + pos, need);
			pos += need;
		}

		for (int x = 0; x < width; ++x) {
			uint8_t r = scanline[size_t(x) * 4 + 0];
			uint8_t g = scanline[size_t(x) * 4 + 1];
			uint8_t b = scanline[size_t(x) * 4 + 2];
			uint8_t e = scanline[size_t(x) * 4 + 3];
			float fr, fg, fb;
			detail::HdrDecodeRgbe(r, g, b, e, fr, fg, fb);
			size_t outIdx = (size_t(y) * size_t(width) + size_t(x)) * 3;
			image.pixels[outIdx + 0] = fr;
			image.pixels[outIdx + 1] = fg;
			image.pixels[outIdx + 2] = fb;
		}
	}

	return Ok(std::move(image));
}

} // namespace render3d
