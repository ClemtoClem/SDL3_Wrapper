// Définitions de data/csv.hpp
#include "data/csv.hpp"

namespace data {

// ── CsvDocument ──────────────────────────────────────────────────────────────

String CsvDocument::EncodeStr() const {
	if (!GetRoot())
		return "";
	auto &columns = GetRoot()->Keys();
	String out;

	for (size_t i = 0; i < columns.size(); ++i) {
		if (i)
			out.Append(',');
		WriteField(out, columns[i]);
	}
	out.Append("\r\n");

	size_t rowCount = 0;
	for (auto &col : columns)
		rowCount = sdl3::Max(rowCount, GetRoot()->Get(col)->GetSize());

	for (size_t row = 0; row < rowCount; ++row) {
		for (size_t i = 0; i < columns.size(); ++i) {
			if (i)
				out.Append(',');
			auto colNode = GetRoot()->Get(columns[i]);
			auto cell = row < colNode->GetSize() ? colNode->At(row) : nullptr;
			WriteField(out, scalar::ToString(cell));
		}
		out.Append("\r\n");
	}
	return out;
}

Option<ParseError> CsvDocument::DecodeImpl(const String &content) {
	std::vector<std::vector<String>> rows;
	auto err = ParseRows(content, rows);
	if (err.IsSome())
		return err;

	auto root = Node::MakeObject();
	if (rows.empty()) {
		SetRoot(root);
		return NONE;
	}

	const auto &header = rows[0];
	for (auto &colName : header)
		root->Set(colName, Node::MakeArray());

	for (size_t r = 1; r < rows.size(); ++r) {
		auto &row = rows[r];
		for (size_t c = 0; c < header.size(); ++c) {
			String cell = c < row.size() ? row[c] : String();
			root->Get(header[c])->Push(scalar::ParseNode(cell.View()));
		}
	}

	SetRoot(root);
	return NONE;
}

void CsvDocument::WriteField(String &out, const String &field) {
	bool needsQuoting =
		field.Contains(',') || field.Contains('"') || field.Contains('\r') || field.Contains('\n');
	if (!needsQuoting) {
		out.Append(field);
		return;
	}
	out.Append('"');
	for (char c : field) {
		if (c == '"')
			out.Append("\"\"");
		else
			out.Append(c);
	}
	out.Append('"');
}

Option<ParseError> CsvDocument::ParseRows(const String &content, std::vector<std::vector<String>> &rows) {
	std::vector<String> row;
	String field;
	bool inQuotes = false;
	size_t i = 0;
	size_t size = content.GetSize();
	int line = 1;

	auto endField = [&] {
		row.push_back(field);
		field.Clear();
	};
	auto endRow = [&] {
		endField();
		rows.push_back(row);
		row.clear();
	};

	while (i < size) {
		char c = content[i];
		if (inQuotes) {
			if (c == '"') {
				if (i + 1 < size && content[i + 1] == '"') {
					field += '"';
					i += 2;
					continue;
				}
				inQuotes = false;
				++i;
				continue;
			}
			if (c == '\n')
				++line;
			field += c;
			++i;
			continue;
		}
		switch (c) {
			case '"':
				inQuotes = true;
				++i;
				continue;
			case ',':
				endField();
				++i;
				continue;
			case '\r':
				++i;
				continue; // ignoré, traité via \n
			case '\n':
				++line;
				endRow();
				++i;
				continue;
			default:
				field += c;
				++i;
				continue;
		}
	}
	if (inQuotes)
		return Some(ParseError("CSV: champ cité non terminé", line));
	if (!field.IsEmpty() || !row.empty())
		endRow();
	return NONE;
}

} // namespace data
