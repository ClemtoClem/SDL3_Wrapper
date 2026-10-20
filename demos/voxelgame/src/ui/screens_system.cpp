// Écrans système : confirmation, chargement, erreur.
#include "common.hpp"

#include <chrono>

namespace voxelgame {

using namespace uiutil;

namespace {

// ── Confirmation ─────────────────────────────────────────────────────────────

class Confirm final : public Screen {
public:
	Confirm(Shell &shell, ConfirmSpec spec) : Screen(shell), m_spec(std::move(spec)) {}
	String Id() const override { return "confirm"; }
	bool Overlay() const override { return true; }

	void Build(ecs::Entity root) override {
		const Palette &p = kit.Colors();
		const sdl3::FColor tone = kit.ToneColor(m_spec.tone);
		const ecs::Entity card = ModalCard(kit, root, 500.f);
		WidgetBuilder head = kit.Row(14.f);
		head.GrowW().Align(ui::CrossAlign::Start).Parent(card);
		const ecs::Entity h = head.Spawn();
		WidgetBuilder badge = f.Column();
		badge.Size(44.f, 44.f).Radius(22.f).Bg(WithAlpha(tone, 0.16f)).Justify(ui::Justify::Center).Align(ui::CrossAlign::Center).Parent(h);
		const ecs::Entity bd = badge.Spawn();
		kit.Icon(m_spec.icon, 24.f, Some(tone)).Parent(bd).Spawn();
		WidgetBuilder text = kit.Column(6.f);
		text.GrowW().Parent(h);
		const ecs::Entity t = text.Spawn();
		kit.Label(m_spec.title, Text::H2).GrowW().TextWrap().Parent(t).Spawn();
		kit.Paragraph(m_spec.message, Text::BODY).Parent(t).Spawn();
		if (!m_spec.details.empty()) {
			WidgetBuilder box = kit.Column(4.f);
			box.GrowW().Pad(math::Sides(14.f, 10.f)).Radius(8.f).Bg(p.bg2).Parent(card);
			const ecs::Entity b = box.Spawn();
			for (const String &d : m_spec.details) {
				WidgetBuilder row = kit.Row(8.f);
				row.Parent(b);
				const ecs::Entity r = row.Spawn();
				kit.Icon(MaterialIcons::INFO, 14.f, Some(p.textDim)).Parent(r).Spawn();
				kit.Label(d, Text::SMALL).TextColor(p.textMuted).Parent(r).Spawn();
			}
		}
		if (!m_spec.checkboxText.IsEmpty()) {
			WidgetBuilder row = kit.Row(10.f);
			row.Parent(card);
			const ecs::Entity r = row.Spawn();
			kit.Check(m_spec.checkboxValue, [this](bool v) {
				m_spec.checkboxValue = v;
				if (m_spec.onCheckbox)
					m_spec.onCheckbox(v);
			}).Parent(r).Spawn();
			kit.Label(m_spec.checkboxText, Text::SMALL).Parent(r).Spawn();
		}
		WidgetBuilder buttons = kit.Row(10.f);
		buttons.GrowW().Justify(ui::Justify::End).Margin(math::Sides(0.f, 6.f, 0.f, 0.f)).Parent(card);
		const ecs::Entity bt = buttons.Spawn();
		kit.Button(m_spec.cancelText, [this] {
			shell.UiSound("ui_cancel");
			auto cancel = m_spec.onCancel;
			shell.Pop();
			if (cancel)
				cancel();
		}).Parent(bt).Spawn();
		kit.Button(m_spec.confirmText, [this] {
			shell.UiSound(m_spec.tone == Tone::DANGER ? "ui_warning" : "ui_confirm");
			auto confirm = m_spec.onConfirm;
			shell.Pop();
			if (confirm)
				confirm();
		}, m_spec.tone == Tone::DANGER ? Tone::DANGER : Tone::PRIMARY).Parent(bt).Spawn();
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
	Loading(Shell &shell, String folder) : Screen(shell), m_folder(std::move(folder)) {
		m_entry = shell.Worlds().Find(m_folder, shell.Mods().Installed());
		m_tip = size_t(std::chrono::steady_clock::now().time_since_epoch().count() / 1000) % Tips().size();
	}
	String Id() const override { return "loading"; }

	void Build(ecs::Entity root) override {
		const Palette &p = kit.Colors();
		const float s = kit.Scale();
		// Fond : vignette du monde (aperçu), sinon le monde des menus assombri.
		if (m_entry.IsSome())
			if (const String key = ImageKey(shell, m_entry.Value().thumbnail); !key.IsEmpty()) {
				WidgetBuilder bg = f.Image(key, 10.f, 10.f);
				bg.Absolute().Anchor(ui::Anchor::TopLeft).W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f)).Fit(ui::ImageFit::COVER);
				bg.Parent(root).Spawn();
			}
		WidgetBuilder veil = f.Column();
		veil.Absolute().Anchor(ui::Anchor::TopLeft).W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f)).Bg(WithAlpha(p.bg0, 0.72f));
		veil.Justify(ui::Justify::Center).Align(ui::CrossAlign::Center).Gap(18.f * s).Parent(root);
		const ecs::Entity v = veil.Spawn();
		const String name = m_entry.IsSome() ? m_entry.Value().name : m_folder;
		kit.PixelTitle(name, 30.f, p.text, true).W(Dimension::Px(900.f * s)).Parent(v).Spawn();
		if (m_entry.IsSome())
			kit.Muted(String::Format("%s · %s · graine %u", GameModeName(m_entry.Value().gameMode).CStr(),
									 DifficultyName(m_entry.Value().difficulty).CStr(), m_entry.Value().seed),
					  Text::SMALL)
				.Parent(v)
				.Spawn();
		WidgetBuilder box = kit.Column(8.f);
		box.W(Dimension::Px(560.f * s)).Parent(v);
		const ecs::Entity b = box.Spawn();
		WidgetBuilder bar = f.Progress(0.f, 1.f, 0.f);
		bar.GrowW().H(Dimension::Px(10.f * s)).Radius(5.f).Parent(b);
		m_bar = bar.Spawn();
		WidgetBuilder statusRow = kit.Row(8.f);
		statusRow.GrowW().Parent(b);
		const ecs::Entity sr = statusRow.Spawn();
		WidgetBuilder spin = f.Spinner(2.5f);
		spin.Size(16.f, 16.f).Parent(sr).Spawn();
		m_status = kit.Muted("Préparation…", Text::SMALL).GrowW().Parent(sr).Spawn();
		m_percent = kit.Label("0 %", Text::SMALL).Parent(sr).Spawn();
		// Conseil et mods chargés.
		WidgetBuilder tip = kit.Row(12.f);
		tip.W(Dimension::Px(560.f * s)).Pad(math::Sides(14.f * s, 12.f * s)).Radius(10.f).Bg(WithAlpha(p.bg1, 0.9f)).BorderColor(p.line, 1.f);
		tip.Align(ui::CrossAlign::Start).Parent(v);
		const ecs::Entity t = tip.Spawn();
		kit.Icon(MaterialIcons::LIGHTBULB, 22.f, Some(p.accent)).Parent(t).Spawn();
		WidgetBuilder tc = kit.Column(3.f);
		tc.GrowW().Parent(t);
		const ecs::Entity tcc = tc.Spawn();
		kit.Label("Conseil", Text::SMALL).TextColor(p.accent).Bold().Parent(tcc).Spawn();
		kit.Paragraph(Tips()[m_tip], Text::SMALL).Parent(tcc).Spawn();
		WidgetBuilder mods = kit.Row(6.f);
		mods.W(Dimension::Px(560.f * s)).Justify(ui::Justify::Center).Parent(v);
		const ecs::Entity m = mods.Spawn();
		for (const voxel::ModInfo &mod : shell.Mods().Installed())
			if (m_entry.IsNone() || m_entry.Value().enabledMods.empty() ||
				std::find(m_entry.Value().enabledMods.begin(), m_entry.Value().enabledMods.end(), mod.id) != m_entry.Value().enabledMods.end())
				kit.Chip(shell.Prefs().ModName(mod.id, mod.name) + String(" ") + mod.version, Tone::NEUTRAL).Parent(m).Spawn();
	}

	void Update(float) override {
		Session *session = shell.Game();
		if (!session)
			return;
		float progress = 0.f;
		String status;
		const bool ready = session->Prepare(progress, status);
		if (!session->Error().IsEmpty()) {
			const String error = session->Error();
			shell.LeaveWorld(false);
			shell.ShowError("Impossible d'ouvrir le monde", String::Format("Le monde « %s » n'a pas pu être chargé.", m_folder.CStr()), error);
			return;
		}
		if (auto pr = kit.Ui().World().GetComponent<ui::UiProgress>(m_bar); pr.IsSome())
			pr.Unwrap()->value = progress;
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
	ecs::Entity m_bar{}, m_status{}, m_percent{};
	size_t m_tip = 0;
};

// ── Erreur et diagnostic ─────────────────────────────────────────────────────

class ErrorScreen final : public Screen {
public:
	ErrorScreen(Shell &shell, String title, String message, String details)
		: Screen(shell), m_title(std::move(title)), m_message(std::move(message)), m_details(std::move(details)) {}
	String Id() const override { return "error"; }
	bool Overlay() const override { return true; }

	void Build(ecs::Entity root) override {
		const Palette &p = kit.Colors();
		const ecs::Entity card = ModalCard(kit, root, 680.f);
		WidgetBuilder head = kit.Row(14.f);
		head.GrowW().Align(ui::CrossAlign::Start).Parent(card);
		const ecs::Entity h = head.Spawn();
		WidgetBuilder badge = f.Column();
		badge.Size(48.f, 48.f).Radius(24.f).Bg(WithAlpha(p.danger, 0.16f)).Justify(ui::Justify::Center).Align(ui::CrossAlign::Center).Parent(h);
		const ecs::Entity bd = badge.Spawn();
		kit.Icon(MaterialIcons::ERROR, 28.f, Some(p.danger)).Parent(bd).Spawn();
		WidgetBuilder text = kit.Column(6.f);
		text.GrowW().Parent(h);
		const ecs::Entity t = text.Spawn();
		kit.Muted("Une erreur est survenue", Text::CAPTION).TextColor(p.danger).Bold().Parent(t).Spawn();
		kit.Label(m_title, Text::H2).GrowW().TextWrap().Parent(t).Spawn();
		kit.Paragraph(m_message, Text::BODY).Parent(t).Spawn();
		// Informations techniques (repliables).
		WidgetBuilder exp = f.Expander("Informations techniques", m_expanded);
		exp.GrowW().HAuto().OnToggle([this](bool v) { m_expanded = v; }).Parent(card);
		const ecs::Entity e = exp.Spawn();
		WidgetBuilder area = f.InputArea();
		area.GrowW().H(Dimension::Px(170.f)).IoMode(ui::IOMode::READ_AND_COPY_ONLY).Monospace().FontSize(kit.Size(Text::SMALL)).Parent(e);
		const ecs::Entity a = area.Spawn();
		if (auto ia = kit.Ui().World().GetComponent<ui::UiInputArea>(a); ia.IsSome())
			ia.Unwrap()->text = Diagnostic();
		WidgetBuilder buttons = kit.Row(10.f);
		buttons.GrowW().Parent(card);
		const ecs::Entity b = buttons.Spawn();
		kit.GhostButton("Copier le diagnostic", [this] {
			(void)SDL_SetClipboardText(Diagnostic().CStr());
			shell.Notify(Tone::SUCCESS, "Diagnostic copié", "Collez-le dans un rapport de bogue.");
		}).Parent(b).Spawn();
		kit.GhostButton("Voir les détails", [this] {
			m_expanded = true;
			shell.Rebuild();
		}).Parent(b).Spawn();
		kit.Spacer().Parent(b).Spawn();
		kit.PrimaryButton("Retour", [this] { shell.Pop(); }).Parent(b).Spawn();
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
};

} // namespace

std::unique_ptr<Screen> MakeConfirm(Shell &shell, ConfirmSpec spec) { return std::make_unique<Confirm>(shell, std::move(spec)); }
std::unique_ptr<Screen> MakeLoading(Shell &shell, String folder) { return std::make_unique<Loading>(shell, std::move(folder)); }
std::unique_ptr<Screen> MakeError(Shell &shell, String title, String message, String details) {
	return std::make_unique<ErrorScreen>(shell, std::move(title), std::move(message), std::move(details));
}

} // namespace voxelgame
