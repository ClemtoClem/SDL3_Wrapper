// Définitions de platform/sdl_log.hpp
#include <platform/sdl_log.hpp>

#include <format>
#include <mutex>

namespace platform {

diagnostics::Level ToDiagnosticsLevel(sdl3::LogPriority priority) noexcept {
	using P = sdl3::LogPriority;
	using L = diagnostics::Level;
	switch (priority) {
	case P::Trace:
	case P::Verbose: return L::Trace;
	case P::Debug: return L::Debug;
	case P::Info: return L::Info;
	case P::Warn: return L::Warn;
	case P::Error: return L::Error;
	case P::Critical: return L::Critical;
	default: return L::Info;
	}
}

size_t RouteSdlLog(diagnostics::Logger &logger) {
	static std::mutex mutex;
	static size_t installed = 0;
	std::lock_guard<std::mutex> lock(mutex);
	if (installed)
		return installed;
	installed = sdl3::LogRouter::Instance().AddSink([&logger](const sdl3::LogMessage &m) {
		const std::string category = std::format("sdl.{}", int(m.category));
		logger.Write(ToDiagnosticsLevel(m.priority), category,
		             std::string_view(m.text.GetData(), m.text.GetSize()));
	});
	return installed;
}

} // namespace platform
