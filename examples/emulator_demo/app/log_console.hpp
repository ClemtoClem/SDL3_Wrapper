#pragma once
/**
 * LogConsole — journal circulaire des messages SDL, branché comme un
 * récepteur (« sink ») de `sdl3::LogRouter` pour toute sa durée de vie.
 *
 * Alimente le panneau de journal de la fenêtre ET le rapport d'exécution
 * (compteurs d'avertissements/erreurs, extrait des derniers). Le routeur peut
 * appeler depuis n'importe quel fil (le cœur journalise depuis le fil
 * d'émulation) : tout est protégé par un mutex.
 */
#include <cstdio>
#include <deque>
#include <mutex>
#include <vector>

#include "sdl3/sdl3.hpp"

namespace emulator_demo::app {

class LogConsole {
public:
	explicit LogConsole(bool echoToStdout = false) : m_echo(echoToStdout) {
		m_sinkId = sdl3::LogRouter::Instance().AddSink([this](const sdl3::LogMessage &message) { Append(message); });
	}
	~LogConsole() { sdl3::LogRouter::Instance().RemoveSink(m_sinkId); }

	LogConsole(const LogConsole &) = delete;
	LogConsole &operator=(const LogConsole &) = delete;

	/// Numéro de version : change à chaque ligne ajoutée (y compris quand la
	/// plus ancienne est évincée), contrairement au nombre de lignes qui
	/// plafonne à MAX_LINES.
	[[nodiscard]] uint64_t Revision() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_revision;
	}

	[[nodiscard]] String AllText() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		String out;
		for (const String &line : m_lines) {
			out += line;
			out += '\n';
		}
		return out;
	}

	[[nodiscard]] long Warnings() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_warnings;
	}
	[[nodiscard]] long Errors() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_errors;
	}
	/// Derniers avertissements et erreurs (au plus MAX_EXCERPT).
	[[nodiscard]] std::vector<String> Excerpt() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return std::vector<String>(m_problems.begin(), m_problems.end());
	}

private:
	static constexpr size_t MAX_LINES = 500;
	static constexpr size_t MAX_EXCERPT = 30;

	void Append(const sdl3::LogMessage &message) {
		const bool isError = int(message.priority) >= int(SDL_LOG_PRIORITY_ERROR);
		const bool isWarning = !isError && int(message.priority) >= int(SDL_LOG_PRIORITY_WARN);
		String text(message.text);
		if (m_echo)
			std::printf("[sdl] %s\n", text.CStr());

		std::lock_guard<std::mutex> lock(m_mutex);
		m_lines.push_back(text);
		while (m_lines.size() > MAX_LINES)
			m_lines.pop_front();
		++m_revision;
		if (isError || isWarning) {
			(isError ? m_errors : m_warnings) += 1;
			m_problems.push_back(String::Format("[%s] %s", isError ? "erreur" : "avert.", text.CStr()));
			while (m_problems.size() > MAX_EXCERPT)
				m_problems.pop_front();
		}
	}

	mutable std::mutex m_mutex;
	std::deque<String> m_lines;
	std::deque<String> m_problems;
	uint64_t m_revision = 0;
	long m_warnings = 0;
	long m_errors = 0;
	size_t m_sinkId = 0;
	bool m_echo;
};

} // namespace emulator_demo::app
