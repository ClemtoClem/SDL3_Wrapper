// Écrans système : confirmation, chargement, erreur.
#include "common.hpp"

#include <chrono>

namespace voxelia {

using namespace uiutil;

namespace {

// ── Confirmation ─────────────────────────────────────────────────────────────

class Confirm final : public Screen {
public:
	Confirm(Shell &shell_, ConfirmSpec spec) : Screen(shell_), m_spec(std::move(spec)) {}
	String Id() const override { return "confirm"; }
	bool Overlay() const override { return true; }

	void Build(ui::Widget root) override {
		const Palette &p = kit.Colors();
		const sdl3::FColor tone = kit.ToneColor(m_spec.tone);
		ui::Widget card = ModalCard(kit, root, 500.f);
		WidgetBuilder head = kit.Row(14.f);
		head.GrowWidth().AlignItems(ui::Align::Start);
		ui::Widget h = card.Append(head);
		WidgetBuilder badge = ui::Column();
		badge.Size(44.f, 44.f).BorderRadius(22.f).Background(WithAlpha(tone, 0.16f)).JustifyContent(ui::Justify::Center).AlignItems(ui::Align::Center);
		ui::Widget bd = h.Append(badge);
		bd.Append(kit.Icon(m_spec.icon, 24.f, Some(tone)));
		WidgetBuilder text = kit.Column(6.f);
		text.GrowWidth();
		ui::Widget t = h.Append(text);
		t.Append(kit.Label(m_spec.title, Text::H2).GrowWidth().TextWrap());
		t.Append(kit.Paragraph(m_spec.message, Text::BODY));
		if (!m_spec.details.empty()) {
			WidgetBuilder box = kit.Column(4.f);
			box.GrowWidth().Padding(math::Sides(14.f, 10.f)).BorderRadius(8.f).Background(p.bg2);
			ui::Widget b = card.Append(box);
			for (const String &d : m_spec.details) {
				WidgetBuilder row = kit.Row(8.f);
				ui::Widget r = b.Append(row);
				r.Append(kit.Icon(MaterialIcons::INFO, 14.f, Some(p.textDim)));
				r.Append(kit.Label(d, Text::SMALL).Color(p.textMuted));
			}
		}
		if (!m_spec.checkboxText.IsEmpty()) {
			WidgetBuilder row = kit.Row(10.f);
			ui::Widget r = card.Append(row);
			r.Append(kit.Check(m_spec.checkboxValue, [this](bool v) {
				m_spec.checkboxValue = v;
				if (m_spec.onCheckbox)
					m_spec.onCheckbox(v);
			}));
			r.Append(kit.Label(m_spec.checkboxText, Text::SMALL));
		}
		WidgetBuilder buttons = kit.Row(10.f);
		buttons.GrowWidth().JustifyContent(ui::Justify::End).Margin(math::Sides(0.f, 6.f, 0.f, 0.f));
		ui::Widget bt = card.Append(buttons);
		bt.Append(kit.Button(m_spec.cancelText, [this] {
			shell.UiSound("ui_cancel");
			auto cancel = m_spec.onCancel;
			shell.Pop();
			if (cancel)
				cancel();
		}));
		bt.Append(kit.Button(m_spec.confirmText, [this] {
			shell.UiSound(m_spec.tone == Tone::DANGER ? "ui_warning" : "ui_confirm");
			auto confirm = m_spec.onConfirm;
			shell.Pop();
			if (confirm)
				confirm();
		}, m_spec.tone == Tone::DANGER ? Tone::DANGER : Tone::PRIMARY));
	}
	bool OnBack() override {
		auto cancel = m_spec.onCancel;
		shell.Pop();
		if (cancel)
			cancel();
		return true;
	}

private:
	ConfirmSpec m_spec;
};

// ── Chargement ───────────────────────────────────────────────────────────────

class Loading final : public Screen {
public:
	Loading(Shell &shell_, String folder) : Screen(shell_), m_folder(std::move(folder)) {
		m_entry = shell.Worlds().Find(m_folder, shell.Mods().Installed());
		m_tip = size_t(std::chrono::steady_clock::now().time_since_epoch().count() / 1000) % Tips().size();
	}
	String Id() const override { return "loading"; }

	void Build(ui::Widget root) override {
		const Palette &p = kit.Colors();
		const float s = kit.Scale();
		// Fond : vignette du monde (aperçu), sinon le monde des menus assombri.
		if (m_entry.IsSome())
			if (const String key = ImageKey(shell, m_entry.Value().thumbnail); !key.IsEmpty()) {
				WidgetBuilder bg = ui::Image(key);
				bg.Absolute().Anchor(ui::Anchor::TopLeft).Width(ui::Length::Pct(100.f)).Height(ui::Length::Pct(100.f)).ObjectFit(ui::ObjectFit::Cover);
				root.Append(bg);
			}
		WidgetBuilder veil = ui::Column();
		veil.Absolute().Anchor(ui::Anchor::TopLeft).Width(ui::Length::Pct(100.f)).Height(ui::Length::Pct(100.f)).Background(WithAlpha(p.bg0, 0.72f));
		veil.JustifyContent(ui::Justify::Center).AlignItems(ui::Align::Center).Gap(18.f * s);
		ui::Widget v = root.Append(veil);
		const String name = m_entry.IsSome() ? m_entry.Value().name : m_folder;
		v.Append(kit.PixelTitle(name, 30.f, p.text, true).Width(900.f * s));
		if (m_entry.IsSome())
			v.Append(kit.Muted(String::Format("%s · %s · graine %u", GameModeName(m_entry.Value().gameMode).CStr(),
									 DifficultyName(m_entry.Value().difficulty).CStr(), m_entry.Value().seed),
					  Text::SMALL));
		WidgetBuilder box = kit.Column(8.f);
		box.Width(560.f * s);
		ui::Widget b = v.Append(box);
		WidgetBuilder bar = ui::Progress(0.f, 1.f, 0.f);
		bar.GrowWidth().Height(10.f * s).BorderRadius(5.f);
		m_bar = b.Append(bar);
		WidgetBuilder statusRow = kit.Row(8.f);
		statusRow.GrowWidth();
		ui::Widget sr = b.Append(statusRow);
		WidgetBuilder spin = ui::Spinner(2.5f);
		sr.Append(spin.Size(16.f, 16.f));
		m_status = sr.Append(kit.Muted("Préparation…", Text::SMALL).GrowWidth());
		m_percent = sr.Append(kit.Label("0 %", Text::SMALL));
		// Conseil et mods chargés.
		WidgetBuilder tip = kit.Row(12.f);
		tip.Width(560.f * s).Padding(math::Sides(14.f * s, 12.f * s)).BorderRadius(10.f).Background(WithAlpha(p.bg1, 0.9f)).BorderColor(p.line).BorderWidth(1.f);
		tip.AlignItems(ui::Align::Start);
		ui::Widget t = v.Append(tip);
		t.Append(kit.Icon(MaterialIcons::LIGHTBULB, 22.f, Some(p.accent)));
		WidgetBuilder tc = kit.Column(3.f);
		tc.GrowWidth();
		ui::Widget tcc = t.Append(tc);
		tcc.Append(kit.Label("Conseil", Text::SMALL).Color(p.accent).Bold());
		tcc.Append(kit.Paragraph(Tips()[m_tip], Text::SMALL));
		WidgetBuilder mods = kit.Row(6.f);
		mods.Width(560.f * s).JustifyContent(ui::Justify::Center);
		ui::Widget m = v.Append(mods);
		for (const voxel::ModInfo &mod : shell.Mods().Installed())
			if (m_entry.IsNone() || m_entry.Value().enabledMods.empty() ||
				std::find(m_entry.Value().enabledMods.begin(), m_entry.Value().enabledMods.end(), mod.id) != m_entry.Value().enabledMods.end())
				m.Append(kit.Chip(shell.Prefs().ModName(mod.id, mod.name) + String(" ") + mod.version, Tone::NEUTRAL));
	}

	void Update(float) override {
		Session *session = shell.Game();
		if (!session)
			return;
		float progress = 0.f;
		String status;
		const bool ready = session->Prepare(progress, status);
		if (!session->Error().IsEmpty()) {
			// LeaveWorld revient au menu et DÉTRUIT cet écran : rien de ses
			// membres ne doit être lu après.
			const String error = session->Error();
			const String message = String::Format("Le monde « %s » n'a pas pu être chargé.", m_folder.CStr());
			Shell &owner = shell;
			owner.LeaveWorld(false);
			owner.ShowError("Impossible d'ouvrir le monde", message, error);
			return;
		}
		m_bar.SetValue(progress);
		kit.SetText(m_status, status);
		kit.SetText(m_percent, String::Format("%d %%", int(progress * 100.f)));
		if (ready) {
			shell.UiSound("ui_confirm");
			shell.ClearScreens();
		}
	}
	bool OnBack() override { return true; }

private:
	String m_folder;
	Option<WorldEntry> m_entry = NONE;
	ui::Widget m_bar{}, m_status{}, m_percent{};
	size_t m_tip = 0;
};

// ── Erreur et diagnostic ─────────────────────────────────────────────────────

class ErrorScreen final : public Screen {
public:
	ErrorScreen(Shell &shell_, String title, String message, String details)
		: Screen(shell_), m_title(std::move(title)), m_message(std::move(message)), m_details(std::move(details)) {}
	String Id() const override { return "error"; }
	bool Overlay() const override { return true; }

	void Build(ui::Widget root) override {
		const Palette &p = kit.Colors();
		ui::Widget card = ModalCard(kit, root, 680.f);
		WidgetBuilder head = kit.Row(14.f);
		head.GrowWidth().AlignItems(ui::Align::Start);
		ui::Widget h = card.Append(head);
		WidgetBuilder badge = ui::Column();
		badge.Size(48.f, 48.f).BorderRadius(24.f).Background(WithAlpha(p.danger, 0.16f)).JustifyContent(ui::Justify::Center).AlignItems(ui::Align::Center);
		ui::Widget bd = h.Append(badge);
		bd.Append(kit.Icon(MaterialIcons::ERROR, 28.f, Some(p.danger)));
		WidgetBuilder text = kit.Column(6.f);
		text.GrowWidth();
		ui::Widget t = h.Append(text);
		t.Append(kit.Muted("Une erreur est survenue", Text::CAPTION).Color(p.danger).Bold());
		t.Append(kit.Label(m_title, Text::H2).GrowWidth().TextWrap());
		t.Append(kit.Paragraph(m_message, Text::BODY));
		// Informations techniques (repliables).
		WidgetBuilder exp = ui::Expander("Informations techniques", m_expanded);
		exp.GrowWidth().OnToggle([this](bool v) { m_expanded = v; });
		ui::Widget e = card.Append(exp);
		// Journal en lecture seule, copiable, en chasse fixe.
		e.Append(ui::LogView().GrowWidth().Height(170.f).FontSize(kit.Size(Text::SMALL)).Ref(m_diagnostic));
		m_diagnostic.SetText(Diagnostic());
		WidgetBuilder buttons = kit.Row(10.f);
		buttons.GrowWidth();
		ui::Widget b = card.Append(buttons);
		b.Append(kit.GhostButton("Copier le diagnostic", [this] {
			(void)SDL_SetClipboardText(Diagnostic().CStr());
			shell.Notify(Tone::SUCCESS, "Diagnostic copié", "Collez-le dans un rapport de bogue.");
		}));
		b.Append(kit.GhostButton("Voir les détails", [this] {
			m_expanded = true;
			shell.Rebuild();
		}));
		b.Append(kit.Spacer());
		b.Append(kit.PrimaryButton("Retour", [this] { shell.Pop(); }));
	}

private:
	String Diagnostic() const {
		String out = m_details;
		out += String::Format("\n\nVoxelia %s — sauvegardes v%d\n", shell.Version().CStr(), voxel::SAVE_VERSION);
		out += String::Format("Mods installés : %d\n", int(shell.Mods().Installed().size()));
		const auto &log = shell.RecentLog();
		for (size_t i = log.size() > 12 ? log.size() - 12 : 0; i < log.size(); ++i)
			out += log[i] + String("\n");
		return out;
	}
	String m_title, m_message, m_details;
	bool m_expanded = false;
	ui::Widget m_diagnostic;
};

} // namespace

std::unique_ptr<Screen> MakeConfirm(Shell &shell, ConfirmSpec spec) { return std::make_unique<Confirm>(shell, std::move(spec)); }
std::unique_ptr<Screen> MakeLoading(Shell &shell, String folder) { return std::make_unique<Loading>(shell, std::move(folder)); }
std::unique_ptr<Screen> MakeError(Shell &shell, String title, String message, String details) {
	return std::make_unique<ErrorScreen>(shell, std::move(title), std::move(message), std::move(details));
}

} // namespace voxelia
