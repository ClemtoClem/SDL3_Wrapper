// Définitions de voxelia/studio/console.hpp
#include <voxelia/studio/console.hpp>

namespace voxelia::studio {

namespace {

constexpr size_t MAX_LINES = 500;
constexpr size_t MAX_HISTORY = 100;

bool IsYes(const String &answer) {
	const String a = answer.Trim().ToLower();
	return a == "y" || a == "yes" || a == "o" || a == "oui";
}

} // namespace

const char *CommandStatusId(CommandStatus status) noexcept {
	switch (status) {
	case CommandStatus::Ok:
		return "ok";
	case CommandStatus::Error:
		return "error";
	case CommandStatus::Warning:
		return "warning";
	case CommandStatus::ConfirmRequired:
		return "confirm";
	case CommandStatus::Pending:
		return "pending";
	}
	return "ok";
}

std::vector<String> SplitCommandLine(const String &line) {
	std::vector<String> out;
	String word;
	bool quoted = false, any = false;
	for (size_t i = 0; i < line.GetSize(); ++i) {
		const char c = line[i];
		if (c == '"') {
			quoted = !quoted;
			any = true;
		} else if (!quoted && (c == ' ' || c == '\t')) {
			if (any)
				out.push_back(word);
			word = String();
			any = false;
		} else {
			word += c;
			any = true;
		}
	}
	if (any)
		out.push_back(word);
	return out;
}

void ConsoleModel::Register(String name, String help, Handler handler) {
	m_commands[std::move(name)] = Entry{std::move(help), std::move(handler)};
}

void ConsoleModel::Open() {
	m_visible = true;
	m_countdown = 0.f;
}

void ConsoleModel::Close() {
	// Une question ou un travail en cours retient la console.
	if (AwaitingConfirmation() || m_pending)
		return;
	m_visible = false;
	m_countdown = 0.f;
}

void ConsoleModel::Update(float dt) {
	if (m_countdown <= 0.f)
		return;
	m_countdown -= dt;
	if (m_countdown <= 0.f) {
		m_countdown = 0.f;
		Close();
	}
}

void ConsoleModel::Print(String text, String kind) {
	m_lines.push_back({std::move(text), std::move(kind)});
	while (m_lines.size() > MAX_LINES)
		m_lines.pop_front();
}

CommandResult ConsoleModel::Submit(const String &line) {
	const String trimmed = line.Trim();
	Open();
	if (AwaitingConfirmation()) {
		Print(trimmed.IsEmpty() ? String("(non)") : trimmed, "input");
		CommandResult question = std::move(m_confirm);
		m_confirm = CommandResult{};
		CommandResult result = IsYes(trimmed) ? question.onConfirm() : CommandResult::Ok(String("annulé"));
		Apply(result);
		return m_last;
	}
	if (trimmed.IsEmpty())
		return m_last;
	Print(trimmed, "input");
	if (m_history.empty() || m_history.back() != trimmed)
		m_history.push_back(trimmed);
	if (m_history.size() > MAX_HISTORY)
		m_history.erase(m_history.begin());
	m_historyCursor = m_history.size();

	const std::vector<String> words = SplitCommandLine(trimmed);
	CommandResult result;
	if (auto it = m_commands.find(words.front()); it != m_commands.end())
		result = it->second.handler(words);
	else if (words.front() == "help") {
		String text("commandes :");
		for (const auto &[name, entry] : m_commands)
			text += String("\n  ") + name + String(" — ") + entry.help;
		result = CommandResult::Ok(text);
	} else if (m_fallback)
		result = m_fallback(trimmed);
	else
		result = CommandResult::Error(String("commande inconnue : ") + words.front());
	Apply(std::move(result));
	return m_last;
}

void ConsoleModel::Resolve(CommandResult result) {
	m_pending = false;
	Apply(std::move(result));
}

void ConsoleModel::Apply(CommandResult result) {
	++m_counts[size_t(result.status)];
	m_countdown = 0.f;
	if (!result.message.IsEmpty())
		Print(result.message, CommandStatusId(result.status));
	switch (result.status) {
	case CommandStatus::Ok:
		m_pending = false;
		if (autoClose)
			Close();
		break;
	case CommandStatus::Error:
		m_visible = true;
		break;
	case CommandStatus::Warning:
		m_visible = true;
		m_countdown = WARNING_SECONDS;
		break;
	case CommandStatus::ConfirmRequired:
		m_visible = true;
		Print(result.confirmationPrompt + String(" [y/N]"), "confirm");
		m_confirm = result;
		break;
	case CommandStatus::Pending:
		m_visible = true;
		m_pending = true;
		break;
	}
	result.onConfirm = nullptr;
	m_last = std::move(result);
}

String ConsoleModel::HistoryStep(int delta) {
	if (m_history.empty())
		return String();
	if (delta < 0 && m_historyCursor > 0)
		--m_historyCursor;
	else if (delta > 0 && m_historyCursor < m_history.size())
		++m_historyCursor;
	return m_historyCursor < m_history.size() ? m_history[m_historyCursor] : String();
}

std::vector<String> ConsoleModel::Complete(const String &partial) const {
	const std::vector<String> words = SplitCommandLine(partial);
	const bool fresh = partial.IsEmpty() || partial.EndsWith(" ");
	const String last = fresh || words.empty() ? String() : words.back();
	const bool first = words.empty() || (words.size() == 1 && !fresh);
	std::vector<String> out;
	if (first) {
		for (const auto &[name, entry] : m_commands)
			if (name.StartsWith(last))
				out.push_back(name);
		if (String("help").StartsWith(last))
			out.push_back(String("help"));
		return out;
	}
	for (const String &w : m_words)
		if (w.StartsWith(last))
			out.push_back(w);
	return out;
}

} // namespace voxelia::studio
