// Définitions de profiler/symbols.hpp
#include <profiler/symbols.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <cxxabi.h>
#include <elf.h>

namespace profiler {

namespace {

bool ReadAll(const String &path, std::vector<uint8_t> &out) {
	FILE *f = std::fopen(path.CStr(), "rb");
	if (!f)
		return false;
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	out.resize(size_t(std::max(0L, size)));
	const size_t n = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
	std::fclose(f);
	return n == out.size();
}

String BaseName(const String &path) {
	const int slash = path.LastIndexOf('/');
	return slash < 0 ? path : path.Substr(size_t(slash) + 1);
}

} // namespace

String Demangle(const char *name) {
	int status = 0;
	char *demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
	if (status == 0 && demangled) {
		String out(demangled);
		std::free(demangled);
		return out;
	}
	std::free(demangled);
	return String(name);
}

Result<ElfSymbols, String> ElfSymbols::Load(const String &path) {
	std::vector<uint8_t> data;
	if (!ReadAll(path, data))
		return Err(path + String(" : illisible"));
	if (data.size() < sizeof(Elf64_Ehdr) || std::memcmp(data.data(), ELFMAG, SELFMAG) != 0 || data[EI_CLASS] != ELFCLASS64)
		return Err(path + String(" : pas un ELF 64 bits"));
	Elf64_Ehdr header;
	std::memcpy(&header, data.data(), sizeof header);
	auto at = [&](uint64_t offset, uint64_t size) { return offset + size <= data.size(); };
	ElfSymbols out;
	// Segments chargés : décalage du fichier ↔ adresse virtuelle.
	for (uint16_t i = 0; i < header.e_phnum; ++i) {
		const uint64_t offset = header.e_phoff + uint64_t(i) * header.e_phentsize;
		if (!at(offset, sizeof(Elf64_Phdr)))
			break;
		Elf64_Phdr ph;
		std::memcpy(&ph, data.data() + offset, sizeof ph);
		if (ph.p_type == PT_LOAD)
			out.m_segments.push_back({ph.p_offset, ph.p_vaddr, ph.p_filesz});
	}
	// Tables de symboles : .symtab (complète) puis .dynsym (exportés).
	std::vector<Elf64_Shdr> sections(header.e_shnum);
	for (uint16_t i = 0; i < header.e_shnum; ++i) {
		const uint64_t offset = header.e_shoff + uint64_t(i) * header.e_shentsize;
		if (!at(offset, sizeof(Elf64_Shdr)))
			return Err(path + String(" : en-têtes de sections tronqués"));
		std::memcpy(&sections[i], data.data() + offset, sizeof(Elf64_Shdr));
	}
	for (uint32_t wanted : {uint32_t(SHT_SYMTAB), uint32_t(SHT_DYNSYM)}) {
		for (const Elf64_Shdr &sh : sections) {
			if (sh.sh_type != wanted || sh.sh_link >= sections.size() || sh.sh_entsize == 0)
				continue;
			const Elf64_Shdr &strings = sections[sh.sh_link];
			if (!at(sh.sh_offset, sh.sh_size) || !at(strings.sh_offset, strings.sh_size))
				continue;
			const size_t count = size_t(sh.sh_size / sh.sh_entsize);
			for (size_t k = 0; k < count; ++k) {
				Elf64_Sym sym;
				std::memcpy(&sym, data.data() + sh.sh_offset + k * sh.sh_entsize, sizeof sym);
				if (ELF64_ST_TYPE(sym.st_info) != STT_FUNC || sym.st_value == 0 || sym.st_name >= strings.sh_size)
					continue;
				const char *name = reinterpret_cast<const char *>(data.data() + strings.sh_offset + sym.st_name);
				out.m_symbols.push_back({sym.st_value, sym.st_size, Demangle(name)});
			}
		}
		if (!out.m_symbols.empty())
			break; // .symtab suffit (elle contient tout .dynsym)
	}
	std::sort(out.m_symbols.begin(), out.m_symbols.end(), [](const ElfSymbol &a, const ElfSymbol &b) {
		return a.address != b.address ? a.address < b.address : a.size > b.size;
	});
	out.m_symbols.erase(std::unique(out.m_symbols.begin(), out.m_symbols.end(),
									[](const ElfSymbol &a, const ElfSymbol &b) { return a.address == b.address; }),
						out.m_symbols.end());
	return Ok(std::move(out));
}

const ElfSymbol *ElfSymbols::Find(uint64_t address) const {
	auto it = std::upper_bound(m_symbols.begin(), m_symbols.end(), address,
							   [](uint64_t a, const ElfSymbol &s) { return a < s.address; });
	if (it == m_symbols.begin())
		return nullptr;
	--it;
	// Taille inconnue (symboles d'assembleur) : jusqu'au suivant.
	if (it->size > 0 && address >= it->address + it->size)
		return nullptr;
	return &*it;
}

Option<uint64_t> ElfSymbols::FileOffsetToAddress(uint64_t offset) const {
	for (const Segment &s : m_segments)
		if (offset >= s.offset && offset < s.offset + s.size)
			return Some(offset - s.offset + s.address);
	return NONE;
}

// ── Symbolizer ──────────────────────────────────────────────────────────────

Symbolizer::Symbolizer(const Capture &capture) : m_capture(capture) {
	(void)FunctionIndex(String("[inconnu]"));
}

uint32_t Symbolizer::FunctionIndex(const String &name) {
	if (auto it = m_functionIndex.find(name); it != m_functionIndex.end())
		return it->second;
	const uint32_t index = uint32_t(m_functions.size());
	m_functions.push_back(name);
	m_functionIndex.emplace(name, index);
	return index;
}

const ElfSymbols *Symbolizer::SymbolsOf(const String &path) {
	if (auto it = m_modules.find(path); it != m_modules.end())
		return it->second.get();
	Result<ElfSymbols, String> loaded = ElfSymbols::Load(path);
	if (loaded.IsError()) {
		m_warnings.push_back(loaded.Error());
		m_modules.emplace(path, nullptr);
		return nullptr;
	}
	auto owned = std::make_unique<ElfSymbols>(std::move(loaded.Value()));
	const ElfSymbols *raw = owned.get();
	m_modules.emplace(path, std::move(owned));
	return raw;
}

const Symbolizer::Location &Symbolizer::Resolve(uint64_t runtime) {
	if (auto it = m_cache.find(runtime); it != m_cache.end())
		return it->second;
	Location loc;
	const Module *module = nullptr;
	for (const Module &m : m_capture.modules)
		if (runtime >= m.start && runtime < m.end) {
			module = &m;
			break;
		}
	if (!module) {
		loc.function = FunctionIndex(String::Format("[0x%llx]", (unsigned long long)runtime));
		return m_cache.emplace(runtime, std::move(loc)).first->second;
	}
	loc.modulePath = module->path;
	loc.module = BaseName(module->path);
	const ElfSymbols *symbols = SymbolsOf(module->path);
	// Adresse de retour : l'appel est juste avant (-1 : dans l'instruction d'appel).
	const uint64_t fileOffset = runtime - 1 - module->start + module->offset;
	if (symbols)
		if (const Option<uint64_t> address = symbols->FileOffsetToAddress(fileOffset); address.IsSome()) {
			loc.address = address.Value();
			if (const ElfSymbol *s = symbols->Find(address.Value())) {
				loc.function = FunctionIndex(s->name);
				return m_cache.emplace(runtime, std::move(loc)).first->second;
			}
		}
	loc.function = FunctionIndex(String::Format("[%s+0x%llx]", loc.module.CStr(), (unsigned long long)fileOffset));
	return m_cache.emplace(runtime, std::move(loc)).first->second;
}

String Symbolizer::SourceLine(uint64_t runtime) {
	if (auto it = m_lines.find(runtime); it != m_lines.end())
		return it->second;
	const Location loc = Resolve(runtime);
	String line;
	if (loc.address != 0 && !loc.modulePath.IsEmpty()) {
		const String command = String::Format("addr2line -e '%s' 0x%llx 2>/dev/null", loc.modulePath.CStr(), (unsigned long long)loc.address);
		if (FILE *p = popen(command.CStr(), "r")) {
			char buffer[1024] = {};
			if (std::fgets(buffer, sizeof buffer, p)) {
				line = String(buffer).Trim();
				if (line.StartsWith("??"))
					line = String();
			}
			pclose(p);
		}
	}
	m_lines.emplace(runtime, line);
	return line;
}

} // namespace profiler
