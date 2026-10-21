// Définitions de diagnostics/assert.hpp
#include <diagnostics/assert.hpp>
#include <diagnostics/log.hpp>

#include <cstdlib>
#include <mutex>

namespace diagnostics {

namespace {
std::mutex g_handlerMutex;
AssertHandler g_handler;
} // namespace

AssertHandler SetAssertHandler(AssertHandler handler) {
	std::lock_guard<std::mutex> lock(g_handlerMutex);
	return std::exchange(g_handler, std::move(handler));
}

void ReportAssertionFailure(std::string_view expression, std::string_view message, const char *file, int line) {
	Logger::Global().Write(Level::Critical, "assert",
						   message.empty() ? std::format("assertion échouée : {} ({}:{})", expression, file, line)
										   : std::format("assertion échouée : {} — {} ({}:{})", expression, message,
														 file, line),
						   file, line);
	AssertHandler handler;
	{
		std::lock_guard<std::mutex> lock(g_handlerMutex);
		handler = g_handler;
	}
	if (handler) {
		handler(AssertionFailure{expression, message, file, line});
		return;
	}
	std::abort();
}

} // namespace diagnostics
