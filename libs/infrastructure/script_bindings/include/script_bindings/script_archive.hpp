#pragma once
/// script_bindings — le module archive:: vu depuis le langage (« Script »),
/// dans l'espace de noms `archive`.
///
/// LECTURE — le format est détecté par les octets, jamais par l'extension :
///
///   let a = archive.open("jeux.7z", {password: "secret"})
///   print(a.format, len(a.entries))          # 7z, 42
///   for (e in a.list("roms/gba"))            # contenu d'un dossier
///       print(e.path, e.type, e.size)
///   print(a.glob("roms/**.sms"))             # motifs * ? [abc] et ** (plusieurs segments)
///   let source = a.read("scripts/init.lua")  # texte (octets bruts)
///   let data = a.read_bytes("bin/monde.bin") # liste d'octets 0-255
///   a.extract("roms/jeu.sms", "/tmp/jeu.sms")   # une entrée → un fichier
///   a.extract_all("dépôt/", {overwrite: true})  # toute l'archive → un dossier
///   a.convert("jeux.zip", "zip")             # réécrit dans un autre format
///
/// NAVIGATION façon shell (répertoire courant de l'archive) :
///
///   a.cd("roms/gba")     print(a.cwd)        # /roms/gba
///   a.list()             a.glob("*.gba")     # relatifs au répertoire courant
///
/// ÉCRITURE — un `archive.writer` (système de fichiers virtuel) vers
/// n'importe quel format inscriptible (RAR : lecture seule) :
///
///   let fs = archive.writer()
///   fs.add_text("monde/notes.txt", "créé par un script")
///   fs.add_bytes("bin/monde.bin", [1, 2, 3])
///   fs.add_file("musiques/thème.ogg", "thème.ogg")  # du disque, en flux
///   fs.add_dir("assets", "assets")                  # un dossier du disque
///   archive.write("sauvegarde.7z", fs, {compression: "lzma2", encryption: "aes256",
///                                      password: "secret"})
///   archive.pack("mon_mod/", "mon_mod.zip")         # un dossier → une archive
///
/// Entrées : tables `{path, name, type, size, packed_size, crc32, method,
/// encrypted, modified_time, mode, link, index}` — `type` vaut "file",
/// "directory" ou "symlink", `crc32` et `link` sont `nil` si inconnus.
///
/// Options de `archive.open` : password, verify (CRC, vrai par défaut),
/// max_entry_size (garde-fou anti-bombe, 4 Gio), cache (mémoire d'un flux
/// solide, 64 Mio). Celles de `archive.write`/`archive.pack`/`convert` :
/// format ("zip", "tar", "7z"… — sinon l'extension), compression ("store",
/// "deflate", "lzma", "lzma2", "bzip2", "zstd", "ppmd"), level (0-9),
/// encryption ("none", "aes256", "zip_crypto"), password, encrypt_headers,
/// solid, dictionary_size, ppmd_order, ppmd_memory, solid_block_size,
/// volume_label, default_time. Celles de `extract_all` : symlinks,
/// overwrite. Une clé inconnue est une erreur.
///
/// Les erreurs du module (format, corruption, mot de passe…) arrivent comme
/// des erreurs de script positionnées, préfixées du nom de la fonction :
/// `archive.open` : mot de passe requis : … — l'API typée
/// (`ArchiveError::ErrorKind`) est ainsi préservée dans le message.
///
/// Fils : le module archive n'est pas sûr entre fils ; toutes les fonctions
/// s'exécutent sur le fil principal (appelées d'un fil `async`, elles y sont
/// renvoyées — cf. Interpreter::PumpMainThread). Une grosse extraction
/// bloque donc le fil principal : la faire hors des scripts d'affichage.
#include <memory>

#include <archive/archive.hpp>
#include <script/script_std.hpp>
#include <script_bindings/script_common.hpp>

namespace script {

namespace archlib {

using lib::Args;
using lib::As;
using lib::Fail;
using lib::TypeBuilder;

/// Une archive ouverte, avec son arborescence et son navigateur.
struct ArchiveObject : HostObject {
        std::shared_ptr<archive::Archive> archive;
};

/// Système de fichiers virtuel en cours d'écriture (`archive.writer`).
struct WriterObject : HostObject {
        std::shared_ptr<archive::VirtualFs> fs;
};

/// Table décrivant une entrée (cf. en-tête) ; `index` : sa position dans
/// `archive::Archive::Entries()` quand elle en vient (NONE pour un dossier
/// implicite du système de fichiers virtuel).
[[nodiscard]] Value EntryToValue(const archive::EntryInfo &entry, Option<size_t> index);

/// Un format depuis son nom ("zip", "tar.gz", "7z"…), avec alias d'extension.
[[nodiscard]] Result<archive::Format, ScriptError> ArgFormat(const String &name, const char *fn);

/// Options de lecture depuis une table (cf. en-tête).
[[nodiscard]] Result<archive::ReadOptions, ScriptError> ReadReadOptions(scriptlib::Options &o);

/// Options d'écriture depuis une table (cf. en-tête) ; rend aussi le format
/// explicite (`NONE` : déduire de l'extension).
[[nodiscard]] Result<std::pair<archive::WriteOptions, Option<archive::Format>>, ScriptError>
ReadWriteOptions(scriptlib::Options &o);

/// Erreur typée du module → erreur de script positionnée.
[[nodiscard]] ScriptError Fault(const archive::ArchiveError &error, const char *fn);

void DefineArchive(TypeBuilder &builder);

void DefineWriter(TypeBuilder &builder);

void InstallFunctions(Interpreter &vm, const std::shared_ptr<const HostType> &archiveType);

} // namespace archlib

/// Installe l'espace de noms `archive` (cf. en-tête) : pure bibliothèque de
/// fichiers, aucune dépendance de l'hôte — ni monde, ni écran, ni moteur.
void InstallArchiveLibrary(Interpreter &vm);

} // namespace script
