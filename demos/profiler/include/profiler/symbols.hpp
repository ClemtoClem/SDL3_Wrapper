#pragma once
/**
 * Symbolisation hors ligne des échantillons de pile (comme Orbit charge les
 * symboles des modules) :
 *
 *  - ElfSymbols : fonctions d'un fichier ELF 64 bits (.symtab, sinon
 *    .dynsym) et ses segments chargés (adresse du fichier ↔ adresse
 *    virtuelle) ;
 *  - Symbolizer : adresse d'exécution → module (carte de la capture) →
 *    adresse virtuelle → fonction (nom C++ démêlé) ; fonctions numérotées
 *    (comparaisons et arbres d'appels sans chaînes) ; ligne de source à la
 *    demande (addr2line, informations de débogage DWARF), mise en cache.
 */
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <core/core.hpp>

#include "capture.hpp"

namespace profiler {

struct ElfSymbol {
	uint64_t address = 0, size = 0;
	String name; ///< démêlé
};

class ElfSymbols {
public:
	[[nodiscard]] static Result<ElfSymbols, String> Load(const String &path);

	/// Fonction qui contient l'adresse VIRTUELLE `address` (nullptr : aucune).
	[[nodiscard]] const ElfSymbol *Find(uint64_t address) const;
	/// Décalage dans le fichier → adresse virtuelle (segments PT_LOAD).
	[[nodiscard]] Option<uint64_t> FileOffsetToAddress(uint64_t offset) const;
	[[nodiscard]] size_t Count() const noexcept { return m_symbols.size(); }

private:
	struct Segment {
		uint64_t offset = 0, address = 0, size = 0;
	};
	std::vector<ElfSymbol> m_symbols; ///< triés par adresse
	std::vector<Segment> m_segments;
};

/// Nom C++ démêlé (inchangé s'il ne l'est pas).
[[nodiscard]] String Demangle(const char *name);

class Symbolizer {
public:
	explicit Symbolizer(const Capture &capture);

	struct Location {
		uint32_t function = 0; ///< indice dans Functions()
		String module;		   ///< nom court du fichier
		String modulePath;
		uint64_t address = 0;  ///< adresse virtuelle dans le module (0 : inconnue)
	};

	/// Adresse d'exécution → fonction (mise en cache par adresse).
	const Location &Resolve(uint64_t runtimeAddress);
	[[nodiscard]] const String &Function(uint32_t index) const { return m_functions[index]; }
	[[nodiscard]] size_t FunctionCount() const noexcept { return m_functions.size(); }
	/// « fichier.cpp:123 » (addr2line), vide si inconnu.
	[[nodiscard]] String SourceLine(uint64_t runtimeAddress);
	/// Modules dont les symboles n'ont pas pu être lus (et pourquoi).
	[[nodiscard]] const std::vector<String> &Warnings() const noexcept { return m_warnings; }

private:
	uint32_t FunctionIndex(const String &name);
	const ElfSymbols *SymbolsOf(const String &path);

	const Capture &m_capture;
	std::unordered_map<String, std::unique_ptr<ElfSymbols>> m_modules; ///< nullptr : illisible
	std::unordered_map<uint64_t, Location> m_cache;
	std::unordered_map<uint64_t, String> m_lines;
	std::vector<String> m_functions;
	std::unordered_map<String, uint32_t> m_functionIndex;
	std::vector<String> m_warnings;
};

} // namespace profiler
