// Définitions de voxelia/studio/structure_document.hpp
#include <voxelia/studio/structure_document.hpp>

#include <algorithm>

#include <voxel/json.hpp>

namespace voxelia::studio {

using voxel::BlockPos;
using voxel::PlotSpec;

StructureDocument::StructureDocument(voxel::Schematic schematic, String name) : m_schematic(std::move(schematic)), m_name(std::move(name)) {}

void StructureDocument::Commit() {
	m_undo.push_back(m_schematic);
	m_redo.clear();
	int64_t cells = 0;
	for (const voxel::Schematic &s : m_undo)
		cells += s.Volume();
	while (m_undo.size() > MAX_UNDO || (m_undo.size() > 1 && cells > MAX_UNDO_CELLS)) {
		cells -= m_undo.front().Volume();
		m_undo.pop_front();
	}
}

void StructureDocument::Changed() {
	m_dirty = true;
	++m_version;
}

bool StructureDocument::Undo() {
	if (m_undo.empty())
		return false;
	m_redo.push_back(std::move(m_schematic));
	m_schematic = std::move(m_undo.back());
	m_undo.pop_back();
	Changed();
	return true;
}

bool StructureDocument::Redo() {
	if (m_redo.empty())
		return false;
	m_undo.push_back(std::move(m_schematic));
	m_schematic = std::move(m_redo.back());
	m_redo.pop_back();
	Changed();
	return true;
}

void StructureDocument::Replace(voxel::Schematic schematic) {
	Commit();
	m_schematic = std::move(schematic);
	Changed();
}

void StructureDocument::Rotate(int quarters) {
	if (((quarters % 4) + 4) % 4 == 0)
		return;
	Commit();
	m_schematic = m_schematic.Rotated(quarters);
	Changed();
}

Result<bool, String> StructureDocument::Mirror(int axis) {
	if (axis < 0 || axis > 2)
		return Err(String("axe x, y ou z attendu"));
	Commit();
	m_schematic = m_schematic.Mirrored(axis);
	Changed();
	return Ok(true);
}

Result<bool, String> StructureDocument::Hollow(int flatAxis) {
	if (flatAxis < -1 || flatAxis > 2)
		return Err(String("axe x, y ou z attendu (ou aucun : volume)"));
	Commit();
	m_schematic = m_schematic.Hollow(flatAxis);
	Changed();
	return Ok(true);
}

Result<int64_t, String> StructureDocument::ReplaceBlock(const String &from, const String &to) {
	voxel::Schematic next = m_schematic;
	const int64_t count = next.Replace(from, to);
	if (count == 0)
		return Ok(int64_t(0)); // rien à remplacer : pas d'étape
	Commit();
	m_schematic = std::move(next);
	Changed();
	return Ok(count);
}

Result<bool, String> StructureDocument::SetCell(const BlockPos &p, const String &block, uint8_t param) {
	const BlockPos &size = m_schematic.Size();
	if (p.x < 0 || p.y < 0 || p.z < 0 || p.x >= size.x || p.y >= size.y || p.z >= size.z)
		return Err(String::Format("cellule hors de la structure (%d × %d × %d)", size.x, size.y, size.z));
	Commit();
	if (block.IsEmpty())
		m_schematic.Clear(p);
	else
		m_schematic.Set(p, block, param);
	Changed();
	return Ok(true);
}

Result<bool, String> StructureDocument::SetAnchor(const BlockPos &p) {
	const BlockPos &size = m_schematic.Size();
	if (p.x < 0 || p.y < 0 || p.z < 0 || p.x >= size.x || p.y >= size.y || p.z >= size.z)
		return Err(String("l'ancre doit être dans la structure"));
	Commit();
	m_schematic.anchor = p;
	Changed();
	return Ok(true);
}

std::vector<std::pair<String, int64_t>> StructureDocument::Counts() const {
	std::vector<std::pair<String, int64_t>> out;
	for (const auto &[block, count] : m_schematic.Counts())
		out.push_back({block, count});
	std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
	return out;
}

std::vector<String> StructureDocument::FormulaVariables(PlotSpec::Kind kind) {
	switch (kind) {
	case PlotSpec::Kind::CURVE:
		return {String("t")};
	case PlotSpec::Kind::SURFACE:
		return {String("u"), String("v")};
	case PlotSpec::Kind::SOLID:
		return {String("u"), String("v"), String("w")};
	case PlotSpec::Kind::IMPLICIT:
	default:
		return {String("x"), String("y"), String("z")};
	}
}

Result<bool, String> StructureDocument::CheckFormula(const PlotSpec &spec) {
	const std::vector<String> variables = FormulaVariables(spec.kind);
	auto check = [&](const char *label, const String &text) -> Result<bool, String> {
		if (text.Trim().IsEmpty())
			return Err(String::Format("%s : formule vide", label));
		if (auto f = voxel::Formula::Compile(text, variables); f.IsError())
			return Err(String::Format("%s : %s", label, f.Error().CStr()));
		return Ok(true);
	};
	if (spec.kind == PlotSpec::Kind::IMPLICIT)
		return check("formule", spec.expression);
	for (const auto &[label, text] : {std::pair{"x", &spec.fx}, std::pair{"y", &spec.fy}, std::pair{"z", &spec.fz}})
		if (auto r = check(label, *text); r.IsError())
			return r;
	return Ok(true);
}

Result<int64_t, String> StructureDocument::Plot(const PlotSpec &spec) {
	if (auto checked = CheckFormula(spec); checked.IsError())
		return Err(checked.Error());
	if (spec.block.IsEmpty())
		return Err(String("bloc du tracé manquant"));
	Result<voxel::Schematic, String> plotted = voxel::Plot(spec);
	if (plotted.IsError())
		return Err(plotted.Error());
	voxel::Schematic schematic = std::move(plotted).Unwrap();
	const int64_t solid = schematic.Filled();
	schematic.name = m_name;
	Replace(std::move(schematic));
	return Ok(solid);
}

Result<StructureDocument, String> StructureDocument::Load(const String &path, const voxel::ContentRegistry &content,
														  std::vector<String> *unknown) {
	Result<voxel::Schematic, String> read = voxel::Schematic::Load(path);
	if (read.IsError())
		return Err(read.Error());
	voxel::Schematic schematic = std::move(read).Unwrap();
	const std::vector<String> missing = schematic.ResolveNames(content, voxel::json::At(content.Config(), "structures.aliases"));
	if (unknown)
		*unknown = missing;
	// Nom : celui du fichier sans ses extensions.
	String name = path;
	if (const size_t slash = name.Rfind('/'); slash != String::NPOS)
		name = name.Substr(slash + 1);
	if (const size_t dot = name.Find('.'); dot != String::NPOS)
		name = name.Substr(0, dot);
	if (schematic.name.IsEmpty())
		schematic.name = name;
	return Ok(StructureDocument(std::move(schematic), name));
}

Result<String, String> StructureDocument::Save(const String &path) {
	if (m_schematic.Empty())
		return Err(String("structure vide"));
	if (voxel::Schematic::FormatOf(path).IsEmpty())
		return Err(String::Format("format inconnu : %s (.vxs.json, .json, .we, .mts)", path.CStr()));
	if (Result<bool, String> written = m_schematic.Save(path); written.IsError())
		return Err(written.Error());
	m_dirty = false;
	return Ok(path);
}

} // namespace voxelia::studio
