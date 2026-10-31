#pragma once
/**
 * Console du studio : chaque commande rend un CODE D'ÉTAT, qui décide de ce
 * que devient la console (modèle sans fenêtre, testé seul ; le panneau ne
 * fait qu'afficher `Visible()`, `Lines()` et `Prompt()`).
 *
 *  - Ok              → la console se ferme (réglable : `autoClose`) ;
 *  - Error           → elle reste ouverte, message en rouge ;
 *  - Warning         → elle reste ouverte 5 s, puis se ferme ;
 *  - ConfirmRequired → elle reste ouverte sur une question « [y/N] » : la
 *                      ligne suivante y répond (y/o/yes/oui confirme) ;
 *  - Pending         → elle reste ouverte jusqu'à `Resolve` (travail long).
 *
 * Les commandes du studio (`studio.*`, `reload…`) sont enregistrées ici ;
 * toute autre ligne part vers l'exécuteur de repli (les commandes du jeu,
 * voxel::Game::Execute) dont le Result devient Ok / Error.
 */
#include <deque>
#include <functional>
#include <map>
#include <vector>

#include <core/core.hpp>

namespace voxelia::studio {

enum class CommandStatus : uint8_t { Ok, Error, Warning, ConfirmRequired, Pending };

[[nodiscard]] const char *CommandStatusId(CommandStatus status) noexcept; ///< "ok", "error"…

struct CommandResult {
	CommandStatus status = CommandStatus::Ok;
	String message{};
	/// ConfirmRequired : la question posée.
	String confirmationPrompt{};
	/// ConfirmRequired : exécuté si l'utilisateur confirme ; son résultat
	/// remplace celui de la question.
	std::function<CommandResult()> onConfirm{};

	[[nodiscard]] static CommandResult Ok(String message = String()) { return {CommandStatus::Ok, std::move(message)}; }
	[[nodiscard]] static CommandResult Error(String message) { return {CommandStatus::Error, std::move(message)}; }
	[[nodiscard]] static CommandResult Warning(String message) { return {CommandStatus::Warning, std::move(message)}; }
	[[nodiscard]] static CommandResult Pending(String message) { return {CommandStatus::Pending, std::move(message)}; }
	[[nodiscard]] static CommandResult Confirm(String prompt, std::function<CommandResult()> onConfirm) {
		CommandResult r{CommandStatus::ConfirmRequired, String(), std::move(prompt)};
		r.onConfirm = std::move(onConfirm);
		return r;
	}
};

/// Ligne affichée (commande saisie ou réponse).
struct ConsoleLine {
	String text;
	/// "input", "ok", "error", "warning", "confirm", "pending", "info".
	String kind;
};

class ConsoleModel {
public:
	using Handler = std::function<CommandResult(const std::vector<String> &args)>;
	using Fallback = std::function<CommandResult(const String &line)>;

	/// Délai de fermeture après un avertissement (secondes).
	static constexpr float WARNING_SECONDS = 5.f;

	/// Commande `name` (premier mot) ; `help` pour la complétion et `help`.
	void Register(String name, String help, Handler handler);
	void SetFallback(Fallback fallback) { m_fallback = std::move(fallback); }
	/// Noms complétables en plus des commandes (blocs, entités, dimensions…).
	void SetCompletionWords(std::vector<String> words) { m_words = std::move(words); }

	// ── Visibilité ──────────────────────────────────────────────────────────
	void Open();
	void Close();
	void Toggle() { m_visible ? Close() : Open(); }
	[[nodiscard]] bool Visible() const noexcept { return m_visible; }
	/// Fermeture automatique sur Ok (défaut : oui).
	bool autoClose = true;
	/// Secondes avant fermeture (avertissement), 0 : aucune.
	[[nodiscard]] float CloseCountdown() const noexcept { return m_countdown; }
	void Update(float dt);

	// ── Saisie ──────────────────────────────────────────────────────────────
	/// Exécute une ligne (ou répond à la confirmation en attente).
	CommandResult Submit(const String &line);
	/// Fin d'une commande Pending (même règles qu'un résultat immédiat).
	void Resolve(CommandResult result);
	[[nodiscard]] bool AwaitingConfirmation() const noexcept { return static_cast<bool>(m_confirm.onConfirm); }
	[[nodiscard]] bool PendingWork() const noexcept { return m_pending; }
	[[nodiscard]] const String &Prompt() const noexcept { return m_confirm.confirmationPrompt; }

	/// Historique : `delta` -1 (plus ancien) / +1 (plus récent) ; rend la ligne.
	[[nodiscard]] String HistoryStep(int delta);
	[[nodiscard]] const std::vector<String> &History() const noexcept { return m_history; }
	/// Candidats pour le dernier mot de `partial` (commandes, puis mots connus).
	[[nodiscard]] std::vector<String> Complete(const String &partial) const;

	[[nodiscard]] const std::deque<ConsoleLine> &Lines() const noexcept { return m_lines; }
	[[nodiscard]] const CommandResult &Last() const noexcept { return m_last; }
	/// Nombre de résultats par statut (rapport).
	[[nodiscard]] size_t Count(CommandStatus status) const noexcept { return m_counts[size_t(status)]; }
	void Print(String text, String kind = String("info"));

private:
	void Apply(CommandResult result);

	struct Entry {
		String help;
		Handler handler;
	};
	std::map<String, Entry> m_commands;
	Fallback m_fallback;
	std::vector<String> m_words;
	std::deque<ConsoleLine> m_lines;
	std::vector<String> m_history;
	size_t m_historyCursor = 0;
	CommandResult m_confirm{};
	CommandResult m_last{};
	size_t m_counts[5] = {0, 0, 0, 0, 0};
	float m_countdown = 0.f;
	bool m_visible = false;
	bool m_pending = false;
};

/// Découpe une ligne en mots (guillemets "…" pour un mot avec espaces).
[[nodiscard]] std::vector<String> SplitCommandLine(const String &line);

} // namespace voxelia::studio
