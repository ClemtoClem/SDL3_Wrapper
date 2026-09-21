#pragma once
/**
 * Wrapper C++ de SDL3/SDL_stdinc.inspiré de SDL3pp_stdinc.h,ais réduit
 * aux parties utiles à ce projet et adaptées à ses conventions (lowerCamelCase,
 * pas de classes RAII pour Environment/IConv/Random qui font double emploi
 * avec la stdlib C++ ou n'ont pas encore d'utilité ici) :
 *
 *  - Math   : fonctions trigonométriques/exponentielles libc-indépendantes de
 *             SDL (utilisées par math/math.hpp au lieu de <cmath>, cohérent
 *             avec le reste du wrapper qui passe toujours par SDL).
 *  - Memory : malloc/calloc/realloc/free (utile pour l'interop avec des API C
 *             SDL qui exigent une mémoire allouée par SDL).
 *  - Ctype  : prédicats de classification de caractères ASCII.
 *  - Env    : lecture/écriture de variables d'environnement du processus.
 *
 * Volontairement absent : qsort/bsearch (std::sort/std::binary_search déjà
 * disponibles et plus sûrs), les fonctions string/wide-string (core/string.hpp
 * couvre déjà ce besoin avec `String`), la classe Time (déjà sdl3::GetTicksMS()),
 * IConv (conversion de charset, hors périmètre actuel), Random (std::mt19937
 * de la stdlib suffit — seuls SDL_rand/SDL_randf bruts sont exposés ici pour
 * qui a besoin du PRNG déterministe interne de SDL spécifiquement).
 */
#include <SDL3/SDL_stdinc.h>
#include <cstddef>
#include <cstdint>

namespace sdl3 {

// ============================================================================
// Math — enveloppent les fonctions math libc-indépendantes de SDL
// ============================================================================

inline constexpr float PI_F = SDL_PI_F;
inline constexpr double PI_D = SDL_PI_D;

[[nodiscard]] inline float DegToRad(float deg) noexcept { return deg * PI_F / 180.f; }
[[nodiscard]] inline double DegToRad(double deg) noexcept { return deg * PI_D / 180.0; }
[[nodiscard]] inline float RadToDeg(float rad) noexcept { return rad * 180.f / PI_F; }
[[nodiscard]] inline double RadToDeg(double rad) noexcept { return rad * 180.0 / PI_D; }

[[nodiscard]] inline float Sqrt(float x) noexcept { return SDL_sqrtf(x); }
[[nodiscard]] inline double Sqrt(double x) noexcept { return SDL_sqrt(x); }
[[nodiscard]] inline float Sin(float x) noexcept { return SDL_sinf(x); }
[[nodiscard]] inline double Sin(double x) noexcept { return SDL_sin(x); }
[[nodiscard]] inline float Cos(float x) noexcept { return SDL_cosf(x); }
[[nodiscard]] inline double Cos(double x) noexcept { return SDL_cos(x); }
[[nodiscard]] inline float Tan(float x) noexcept { return SDL_tanf(x); }
[[nodiscard]] inline double Tan(double x) noexcept { return SDL_tan(x); }
[[nodiscard]] inline float Asin(float x) noexcept { return SDL_asinf(x); }
[[nodiscard]] inline double Asin(double x) noexcept { return SDL_asin(x); }
[[nodiscard]] inline float Acos(float x) noexcept { return SDL_acosf(x); }
[[nodiscard]] inline double Acos(double x) noexcept { return SDL_acos(x); }
[[nodiscard]] inline float Atan(float x) noexcept { return SDL_atanf(x); }
[[nodiscard]] inline double Atan(double x) noexcept { return SDL_atan(x); }
[[nodiscard]] inline float Atan2(float y, float x) noexcept { return SDL_atan2f(y, x); }
[[nodiscard]] inline double Atan2(double y, double x) noexcept { return SDL_atan2(y, x); }

[[nodiscard]] inline float Ceil(float x) noexcept { return SDL_ceilf(x); }
[[nodiscard]] inline double Ceil(double x) noexcept { return SDL_ceil(x); }
[[nodiscard]] inline float Floor(float x) noexcept { return SDL_floorf(x); }
[[nodiscard]] inline double Floor(double x) noexcept { return SDL_floor(x); }
[[nodiscard]] inline float Round(float x) noexcept { return SDL_roundf(x); }
[[nodiscard]] inline double Round(double x) noexcept { return SDL_round(x); }
[[nodiscard]] inline float Trunc(float x) noexcept { return SDL_truncf(x); }
[[nodiscard]] inline double Trunc(double x) noexcept { return SDL_trunc(x); }

[[nodiscard]] inline float Exp(float x) noexcept { return SDL_expf(x); }
[[nodiscard]] inline double Exp(double x) noexcept { return SDL_exp(x); }
[[nodiscard]] inline float Log(float x) noexcept { return SDL_logf(x); }
[[nodiscard]] inline double Log(double x) noexcept { return SDL_log(x); }
[[nodiscard]] inline float Log10(float x) noexcept { return SDL_log10f(x); }
[[nodiscard]] inline double Log10(double x) noexcept { return SDL_log10(x); }
[[nodiscard]] inline float Pow(float x, float y) noexcept { return SDL_powf(x, y); }
[[nodiscard]] inline double Pow(double x, double y) noexcept { return SDL_pow(x, y); }
[[nodiscard]] inline float Fmod(float x, float y) noexcept { return SDL_fmodf(x, y); }
[[nodiscard]] inline double Fmod(double x, double y) noexcept { return SDL_fmod(x, y); }
[[nodiscard]] inline float Copysign(float x, float sign) noexcept { return SDL_copysignf(x, sign); }
[[nodiscard]] inline double Copysign(double x, double sign) noexcept { return SDL_copysign(x, sign); }

[[nodiscard]] inline float Abs(float x) noexcept { return SDL_fabsf(x); }
[[nodiscard]] inline double Abs(double x) noexcept { return SDL_fabs(x); }
[[nodiscard]] inline int Abs(int x) noexcept { return SDL_abs(x); }

[[nodiscard]] inline float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

template <typename T> [[nodiscard]] constexpr T Min(T a, T b) noexcept { return a < b ? a : b; }
template <typename T> [[nodiscard]] constexpr T Max(T a, T b) noexcept { return a > b ? a : b; }
template <typename T> [[nodiscard]] constexpr T Clamp(T v, T lo, T hi) noexcept { return Min(Max(v, lo), hi); }

// ============================================================================
// Memory — malloc/calloc/realloc/free bruts de SDL (utile pour l'interop
// avec des API C SDL qui exigent une mémoire allouée/libérée via SDL).
// ============================================================================

namespace memory {

[[nodiscard]] inline void *malloc(size_t size) noexcept { return SDL_malloc(size); }
[[nodiscard]] inline void *calloc(size_t count, size_t size) noexcept { return SDL_calloc(count, size); }
[[nodiscard]] inline void *realloc(void *mem, size_t size) noexcept { return SDL_realloc(mem, size); }
inline void free(void *mem) noexcept { SDL_free(mem); }

} // namespace memory

// ============================================================================
// Ctype — classification de caractères ASCII (retournent bool plutôt que
// l'int C historique).
// ============================================================================

namespace ctype {

[[nodiscard]] inline bool IsAlpha(char c) noexcept { return SDL_isalpha(uint8_t(c)); }
[[nodiscard]] inline bool IsDigit(char c) noexcept { return SDL_isdigit(uint8_t(c)); }
[[nodiscard]] inline bool IsAlnum(char c) noexcept { return SDL_isalnum(uint8_t(c)); }
[[nodiscard]] inline bool IsSpace(char c) noexcept { return SDL_isspace(uint8_t(c)); }
[[nodiscard]] inline bool IsBlank(char c) noexcept { return SDL_isblank(uint8_t(c)); }
[[nodiscard]] inline bool IsUpper(char c) noexcept { return SDL_isupper(uint8_t(c)); }
[[nodiscard]] inline bool IsLower(char c) noexcept { return SDL_islower(uint8_t(c)); }
[[nodiscard]] inline bool IsPrint(char c) noexcept { return SDL_isprint(uint8_t(c)); }
[[nodiscard]] inline bool IsGraph(char c) noexcept { return SDL_isgraph(uint8_t(c)); }
[[nodiscard]] inline bool IsPunct(char c) noexcept { return SDL_ispunct(uint8_t(c)); }
[[nodiscard]] inline bool IsCntrl(char c) noexcept { return SDL_iscntrl(uint8_t(c)); }
[[nodiscard]] inline bool IsXDigit(char c) noexcept { return SDL_isxdigit(uint8_t(c)); }

[[nodiscard]] inline char ToUpper(char c) noexcept { return char(SDL_toupper(uint8_t(c))); }
[[nodiscard]] inline char ToLower(char c) noexcept { return char(SDL_tolower(uint8_t(c))); }

} // namespace ctype

// ============================================================================
// Environment — variables d'environnement du processus.
// Les variantes "_unsafe" de SDL ne sont pas thread-safe si l'environnement
// est modifié concurremment ; comme un process de jeu/app SDL ne le fait
// typiquement pas en dehors du démarrage, elles suffisent ici (cf. doc SDL
// pour la classe Environment thread-safe si un jour nécessaire).
// ============================================================================

namespace env {

[[nodiscard]] inline const char *Get(const char *name) noexcept { return SDL_getenv(name); }
inline bool Set(const char *name, const char *value, bool overwrite = true) noexcept {
	return SDL_setenv_unsafe(name, value, overwrite) == 0;
}
inline bool Unset(const char *name) noexcept { return SDL_unsetenv_unsafe(name) == 0; }

} // namespace env

} // namespace sdl3
