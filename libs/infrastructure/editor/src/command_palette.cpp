// Définitions de command_palette.hpp
#include <editor/command_palette.hpp>

#include <algorithm>

namespace editor {

namespace {

char Lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

bool WordStart(const String &text, size_t i) noexcept {
	if (i == 0)
		return true;
	const char p = text[i - 1];
	return p == ' ' || p == '.' || p == '_' || p == '-' || p == ':' || p == '/' || (p >= 'a' && p <= 'z' && text[i] >= 'A' && text[i] <= 'Z');
}

} // namespace

Option<int> FuzzyScore(const String &query, const String &text) {
	if (query.Length() == 0)
		return Some(0);
	int score = 0;
	size_t q = 0;
	Option<size_t> previous = NONE;
	for (size_t i = 0; i < text.Length() && q < query.Length(); ++i) {
		if (query[q] == ' ') { // espace de la requête : saute
			++q;
			--i;
			continue;
		}
		if (Lower(text[i]) != Lower(query[q]))
			continue;
		score += 10;
		if (WordStart(text, i))
			score += 20;
		if (previous.IsSome() && previous.Value() + 1 == i)
			score += 15;
		else if (previous.IsSome())
			score -= int(std::min<size_t>(i - previous.Value(), 10));
		if (i == 0)
			score += 10;
		previous = Some(i);
		++q;
	}
	while (q < query.Length() && query[q] == ' ')
		++q;
	if (q < query.Length())
		return NONE;
	return Some(score - int(std::min<size_t>(text.Length(), 60) / 6));
}

void CommandPalette::Add(PaletteEntry entry) {
	for (PaletteEntry &e : m_entries)
		if (e.id == entry.id) {
			e = std::move(entry);
			return;
		}
	m_entries.push_back(std::move(entry));
}

void CommandPalette::Clear() noexcept { m_entries.clear(); }

std::vector<size_t> CommandPalette::Search(const String &query, size_t limit) const {
	struct Scored {
		size_t index;
		int score;
		int recent;
	};
	std::vector<Scored> scored;
	for (size_t i = 0; i < m_entries.size(); ++i) {
		const PaletteEntry &e = m_entries[i];
		Option<int> s = FuzzyScore(query, e.title);
		if (const Option<int> byId = FuzzyScore(query, e.id); byId.IsSome() && (s.IsNone() || byId.Value() > s.Value()))
			s = byId;
		if (s.IsNone())
			if (const Option<int> byHelp = FuzzyScore(query, e.help); byHelp.IsSome())
				s = Some(byHelp.Value() / 2);
		if (s.IsNone())
			continue;
		const auto at = std::find(m_recent.begin(), m_recent.end(), e.id);
		const int recent = at == m_recent.end() ? 0 : int(m_recent.size() - size_t(at - m_recent.begin()));
		scored.push_back({i, s.Value(), recent});
	}
	std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
		return a.score != b.score ? a.score > b.score : a.recent > b.recent;
	});
	std::vector<size_t> out;
	for (size_t i = 0; i < scored.size() && i < limit; ++i)
		out.push_back(scored[i].index);
	return out;
}

bool CommandPalette::Run(size_t index) {
	if (index >= m_entries.size() || !m_entries[index].run)
		return false;
	const String id = m_entries[index].id;
	std::erase(m_recent, id);
	m_recent.insert(m_recent.begin(), id);
	if (m_recent.size() > 20)
		m_recent.pop_back();
	m_entries[index].run(); // peut modifier la palette : en dernier
	return true;
}

// ── Vue ─────────────────────────────────────────────────────────────────────

void CommandPaletteView::Build(ui::Widget parent) {
	m_modal = parent.Append(ui::Modal(ui::Column().Class("command-palette").Children({
		ui::Input(String("Commande…")).Class("command-palette-input").Id("command-palette-input").GrowWidth().Ref(m_input)
			.OnTextChange([this](const String &text) { SetQuery(text); })
			.OnSubmit([this](const String &) { (void)Accept(); })
			.OnKey(sdl3::keyboard::Key::UP, [this] { Move(-1); })
			.OnKey(sdl3::keyboard::Key::DOWN, [this] { Move(1); }),
		ui::Column().Class("command-palette-list").Id("command-palette-list").GrowWidth().Ref(m_list),
	})).Id("command-palette"));
}

void CommandPaletteView::Open() {
	if (!m_modal)
		return;
	m_query = String();
	m_input.SetText(String());
	m_current = 0;
	Refresh();
	m_modal.Open();
	m_input.Focus();
}

void CommandPaletteView::Close() {
	if (m_modal && m_modal.IsOpen())
		m_modal.Close();
}

bool CommandPaletteView::IsOpen() const { return m_modal && m_modal.IsOpen(); }

void CommandPaletteView::SetQuery(const String &query) {
	m_query = query;
	m_current = 0;
	Refresh();
}

bool CommandPaletteView::Accept() {
	if (m_current < 0 || size_t(m_current) >= m_shown.size())
		return false;
	const size_t index = m_shown[size_t(m_current)];
	Close();
	return m_model.Run(index);
}

void CommandPaletteView::Move(int delta) {
	if (m_shown.empty())
		return;
	const int previous = m_current;
	m_current = std::clamp(m_current + delta, 0, int(m_shown.size()) - 1);
	if (previous == m_current || size_t(previous) >= m_rows.size())
		return;
	m_rows[size_t(previous)].RemoveClass("current");
	m_rows[size_t(m_current)].AddClass(String("current"));
	m_rows[size_t(m_current)].ScrollIntoView();
}

void CommandPaletteView::Refresh() {
	m_shown = m_model.Search(m_query);
	m_rows.clear();
	if (!m_list)
		return;
	m_list.Clear();
	for (size_t i = 0; i < m_shown.size(); ++i) {
		const PaletteEntry &e = m_model.Entry(m_shown[i]);
		const int row = int(i);
		ui::WidgetBuilder b = ui::Row();
		b.Class("command-palette-row").GrowWidth();
		if (row == m_current)
			b.Class("current");
		b.Children({
			ui::Column().Grow().Gap(0.f).PointerThrough().Children({
				ui::Label(e.title).Class("command-palette-title").PointerThrough(),
				ui::Label(e.help.Length() ? e.help : e.id).Class("command-palette-help").PointerThrough(),
			}),
			ui::Label(e.shortcut).Class("command-palette-shortcut").PointerThrough(),
		});
		b.Pressable().OnClick([this, row] {
			m_current = row;
			(void)Accept();
		});
		m_rows.push_back(m_list.Append(b));
	}
}

} // namespace editor
