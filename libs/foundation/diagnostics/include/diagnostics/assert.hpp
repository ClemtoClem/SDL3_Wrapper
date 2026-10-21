#pragma once
/**
 * Assertions de diagnostics::.
 *
 *   DIAG_ASSERT(cond, "fmt", args...)  vérifiée seulement si DIAG_ENABLE_ASSERTS
 *                                      (défini par défaut hors NDEBUG) ;
 *   DIAG_VERIFY(cond, "fmt", args...)  toujours évaluée, rend `cond` (bool) :
 *                                      if (!DIAG_VERIFY(ptr, "…")) return;
 *
 * Un échec journalise un message Critical (catégorie "assert") puis appelle
 * le gestionnaire d'assertion. Le gestionnaire par défaut interrompt le
 * programme (std::abort) ; SetAssertHandler permet de le remplacer, par
 * exemple pour qu'un test vérifie qu'une assertion échoue sans s'arrêter.
 * Aucun mécanisme d'exception n'est utilisé.
 */
#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace diagnostics {

struct AssertionFailure {
	std::string_view expression;
	std::string_view message;
	const char *file;
	int line;
};

using AssertHandler = std::function<void(const AssertionFailure &)>;

/// Remplace le gestionnaire ; rend le précédent. nullptr restaure le défaut.
AssertHandler SetAssertHandler(AssertHandler handler);

/// Journalise l'échec puis appelle le gestionnaire courant.
void ReportAssertionFailure(std::string_view expression, std::string_view message, const char *file, int line);

namespace detail {
inline std::string AssertMessage() { return {}; }
template <class... Args> std::string AssertMessage(std::format_string<Args...> fmt, Args &&...args) {
	return std::format(fmt, std::forward<Args>(args)...);
}
} // namespace detail

} // namespace diagnostics

#if !defined(DIAG_ENABLE_ASSERTS) && !defined(NDEBUG)
#define DIAG_ENABLE_ASSERTS 1
#endif

#define DIAG_VERIFY(cond, ...)		\
	([&]() -> bool {				\
		if (cond) return true;		\
		::diagnostics::ReportAssertionFailure(#cond, ::diagnostics::detail::AssertMessage(__VA_ARGS__), __FILE__, __LINE__); \                                                            \
		return false;				\
	}())

#if defined(DIAG_ENABLE_ASSERTS) && (DIAG_ENABLE_ASSERTS == 1)
#define DIAG_ASSERT(cond, ...) ((void)DIAG_VERIFY(cond __VA_OPT__(, ) __VA_ARGS__))
#else
#define DIAG_ASSERT(cond, ...) ((void)0)
#endif
