// Définitions de sdl3/sdl_context.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/sdl_context.hpp"

namespace sdl3 {

// ── SdlContext ───────────────────────────────────────────────────────────────

SdlContext::~SdlContext() {
	if (owns)
		SDL_Quit();
}

Result<SdlContext, Error> SdlContext::Create(InitFlags flags) {
	SdlContext ctx(flags);
	if (!ctx)
		return Err(GetError());
	return Ok(std::move(ctx));
}

} // namespace sdl3
