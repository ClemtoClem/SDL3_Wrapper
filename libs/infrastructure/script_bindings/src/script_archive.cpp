// Définitions de script_bindings/script_archive.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include <script/script.hpp>
#include <script_bindings/script_archive.hpp>

#include <algorithm>
#include <utility>

namespace script {

namespace archlib {

// ============================================================================
// Conversions
// ============================================================================

ScriptError Fault(const archive::ArchiveError &error, const char *fn) {
        return Fail(String::Format("`%s` : %s", fn, error.Describe().CStr()));
}

Value EntryToValue(const archive::EntryInfo &entry, Option<size_t> index) {
        auto map = std::make_shared<MapObject>();
        map->SetKey(String("path"), Value::Str(entry.path));
        map->SetKey(String("name"), Value::Str(entry.BaseName()));
        map->SetKey(String("type"), Value::Str(entry.IsDirectory() ? "directory"
                                                                                         : entry.type == archive::EntryType::SYMLINK ? "symlink"
                                                                                                                                                                                 : "file"));
        map->SetKey(String("size"), Value::UInt(entry.size));
        map->SetKey(String("packed_size"), Value::UInt(entry.packedSize));
        map->SetKey(String("crc32"), entry.crc32.IsSome() ? Value::UInt(entry.crc32.Value(), NumberType::U32) : Value::Nil());
        map->SetKey(String("method"), Value::Str(entry.method));
        map->SetKey(String("encrypted"), Value::Boolean(entry.encrypted));
        map->SetKey(String("modified_time"), Value::Int(entry.modifiedTime));
        map->SetKey(String("mode"), Value::UInt(entry.mode, NumberType::U32));
        map->SetKey(String("link"), entry.linkTarget.IsEmpty() ? Value::Nil() : Value::Str(entry.linkTarget));
        map->SetKey(String("index"), index.IsSome() ? Value::UInt(uint64_t(index.Value())) : Value::Nil());
        return Value::Map(std::move(map));
}

Result<archive::Format, ScriptError> ArgFormat(const String &name, const char *fn) {
        const String key = name.ToLower();
        static const std::pair<const char *, archive::Format> FORMATS[] = {
                {"zip", archive::Format::ZIP},                  {"gzip", archive::Format::GZIP},
                {"gz", archive::Format::GZIP},                  {"xz", archive::Format::XZ},
                {"bzip2", archive::Format::BZIP2},              {"bz2", archive::Format::BZIP2},
                {"zstd", archive::Format::ZSTD},                {"zst", archive::Format::ZSTD},
                {"tar", archive::Format::TAR},                  {"tar.gz", archive::Format::TAR_GZIP},
                {"tgz", archive::Format::TAR_GZIP},             {"tar.xz", archive::Format::TAR_XZ},
                {"tar.bz2", archive::Format::TAR_BZIP2},{"tar.zst", archive::Format::TAR_ZSTD},
                {"7z", archive::Format::SEVEN_ZIP},             {"iso", archive::Format::ISO9660},
                {"iso9660", archive::Format::ISO9660},  {"rar", archive::Format::RAR},
        };
        for (const auto &[label, format] : FORMATS)
                if (key == label)
                        return Ok(format);
        return Err(Fail(String::Format("`%s` : format `%s` inconnu (zip, gzip, xz, bzip2, zstd, tar, tar.gz, tar.xz, "
                                                                   "tar.bz2, tar.zst, 7z, iso, rar)",
                                                                   fn, name.CStr())));
}

Result<archive::ReadOptions, ScriptError> ReadReadOptions(scriptlib::Options &o) {
        archive::ReadOptions options;
        options.password = o.Text("password", "");
        options.verifyChecksums = o.Bool("verify", true);
        options.maxEntrySize = uint64_t(o.Int64("max_entry_size", int64_t(1) << 32));
        options.memoryCacheLimit = uint64_t(o.Int64("cache", int64_t(64) << 20));
        if (auto error = o.Finish(); error.IsSome())
                return Err(error.Unwrap());
        return Ok(std::move(options));
}

Result<std::pair<archive::WriteOptions, Option<archive::Format>>, ScriptError>
ReadWriteOptions(scriptlib::Options &o) {
        archive::WriteOptions options;
        Option<archive::Format> format = NONE;
        if (Option<String> named = o.Str("format"); named.IsSome()) {
                auto parsed = ArgFormat(named.Value(), "archive");
                if (parsed.IsError())
                        return Err(parsed.Error());
                format = Some(parsed.Unwrap());
        }
        if (Option<String> named = o.Str("compression"); named.IsSome()) {
                const String key = named.Value().ToLower();
                static const std::pair<const char *, archive::Compression> COMPRESSIONS[] = {
                        {"store", archive::Compression::STORE},   {"stockée", archive::Compression::STORE},
                        {"deflate", archive::Compression::DEFLATE}, {"lzma", archive::Compression::LZMA},
                        {"lzma2", archive::Compression::LZMA2},   {"bzip2", archive::Compression::BZIP2},
                        {"zstd", archive::Compression::ZSTD},     {"ppmd", archive::Compression::PPMD},
                };
                bool found = false;
                for (const auto &[label, compression] : COMPRESSIONS)
                        if (key == label) {
                                options.compression = compression;
                                found = true;
                        }
                if (!found)
                        o.Fault(String::Format("`archive` : compression `%s` inconnue (store, deflate, lzma, lzma2, bzip2, zstd, ppmd)",
                                                                   named.Value().CStr()));
        }
        options.level = std::clamp(o.Int("level", options.level), 0, 9);
        if (Option<String> named = o.Str("encryption"); named.IsSome()) {
                const String key = named.Value().ToLower();
                if (key == "none" || key.IsEmpty())
                        options.encryption = archive::Encryption::NONE;
                else if (key == "aes256" || key == "aes-256")
                        options.encryption = archive::Encryption::AES256;
                else if (key == "zip_crypto" || key == "zipcrypto")
                        options.encryption = archive::Encryption::ZIP_CRYPTO;
                else
                        o.Fault(String::Format("`archive` : chiffrement `%s` inconnu (none, aes256, zip_crypto)", named.Value().CStr()));
        }
        options.password = o.Text("password", "");
        options.encryptHeaders = o.Bool("encrypt_headers", options.encryptHeaders);
        options.solid = o.Bool("solid", options.solid);
        options.dictionarySize = uint32_t(o.Int64("dictionary_size", int64_t(options.dictionarySize)));
        options.ppmdOrder = uint32_t(std::clamp(o.Int("ppmd_order", int(options.ppmdOrder)), 2, 64));
        options.ppmdMemoryMb = uint32_t(std::clamp(o.Int("ppmd_memory", int(options.ppmdMemoryMb)), 1, 2048));
        options.solidBlockSize = uint64_t(o.Int64("solid_block_size", int64_t(options.solidBlockSize)));
        if (Option<String> label = o.Str("volume_label"); label.IsSome())
                options.volumeLabel = label.Value();
        options.defaultTime = o.Int64("default_time", options.defaultTime);
        if (auto error = o.Finish(); error.IsSome())
                return Err(std::move(error).Unwrap());
        return Ok(std::make_pair(std::move(options), format));
}

// ============================================================================
// archive.archive
// ============================================================================

void DefineArchive(TypeBuilder &builder) {
        auto archiveOf = [](const HostRef &self) -> archive::Archive & { return *As<ArchiveObject>(self).archive; };

        builder.Method("set_password", 1, 1, [archiveOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                archiveOf(self).SetPassword(a[0].ToDisplayString());
                return Ok(Value::Host(self));
        });
        builder.Method("read", 1, 1, [archiveOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                auto bytes = archiveOf(self).Read(a[0].ToDisplayString());
                if (bytes.IsError())
                        return Err(Fault(bytes.Error(), "archive.read"));
                const archive::Bytes &data = bytes.Value();
                return Ok(Value::Str(String(reinterpret_cast<const char *>(data.data()), data.size())));
        });
        builder.Method("read_bytes", 1, 1, [archiveOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                auto bytes = archiveOf(self).Read(a[0].ToDisplayString());
                if (bytes.IsError())
                        return Err(Fault(bytes.Error(), "archive.read_bytes"));
                return Ok(scriptlib::BytesList(bytes.Value()));
        });
        builder.Method("entry", 1, 1, [archiveOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                archive::Archive &ar = archiveOf(self);
                auto found = ar.Nav().Resolve(a[0].ToDisplayString());
                if (found.IsNone())
                        return Ok(Value::Nil());
                const archive::VirtualFs::Node &node = ar.Fs().Get(found.Value());
                return Ok(EntryToValue(node.info, node.entryIndex));
        });
        builder.Method("list", 0, 1, [archiveOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                archive::Archive &ar = archiveOf(self);
                const std::vector<archive::VirtualFs::NodeId> nodes = ar.Nav().List(a.empty() ? String("") : a[0].ToDisplayString());
                auto list = std::make_shared<ListObject>();
                list->items.reserve(nodes.size());
                for (archive::VirtualFs::NodeId id : nodes) {
                        const archive::VirtualFs::Node &node = ar.Fs().Get(id);
                        list->items.push_back(EntryToValue(node.info, node.entryIndex));
                }
                return Ok(Value::List(std::move(list)));
        });
        builder.Method("glob", 1, 1, [archiveOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                archive::Archive &ar = archiveOf(self);
                const std::vector<archive::VirtualFs::NodeId> nodes = ar.Nav().Glob(a[0].ToDisplayString());
                auto list = std::make_shared<ListObject>();
                list->items.reserve(nodes.size());
                for (archive::VirtualFs::NodeId id : nodes)
                        list->items.push_back(Value::Str(ar.Fs().PathOf(id)));
                return Ok(Value::List(std::move(list)));
        });
        builder.Method("cd", 1, 1, [archiveOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                auto moved = archiveOf(self).Nav().ChangeDirectory(a[0].ToDisplayString());
                if (moved.IsError())
                        return Err(Fault(moved.Error(), "archive.cd"));
                return Ok(Value::Boolean(moved.Value()));
        });
        builder.Method("extract", 2, 2, [archiveOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                auto bytes = archiveOf(self).Read(a[0].ToDisplayString());
                if (bytes.IsError())
                        return Err(Fault(bytes.Error(), "archive.extract"));
                const archive::Bytes &data = bytes.Value();
                const String path = a[1].ToDisplayString();
                if (!sdl3::WriteFile(path, reinterpret_cast<const char *>(data.data()), data.size()))
                        return Err(Fail(String::Format("`archive.extract` : écriture de %s impossible", path.CStr())));
                return Ok(Value::Boolean(true));
        });
        builder.Method("extract_all", 1, 2, [archiveOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                scriptlib::Options o(a, 1, "archive.extract_all");
                archive::ExtractOptions options;
                options.createSymlinks = o.Bool("symlinks", options.createSymlinks);
                options.overwrite = o.Bool("overwrite", options.overwrite);
                if (auto error = o.Finish(); error.IsSome())
                        return Err(error.Unwrap());
                auto stats = archiveOf(self).ExtractAll(a[0].ToDisplayString(), options);
                if (stats.IsError())
                        return Err(Fault(stats.Error(), "archive.extract_all"));
                auto map = std::make_shared<MapObject>();
                map->SetKey(String("files"), Value::UInt(stats.Value().files));
                map->SetKey(String("directories"), Value::UInt(stats.Value().directories));
                map->SetKey(String("links"), Value::UInt(stats.Value().links));
                map->SetKey(String("skipped_links"), Value::UInt(stats.Value().skippedLinks));
                map->SetKey(String("skipped_files"), Value::UInt(stats.Value().skippedFiles));
                map->SetKey(String("bytes"), Value::UInt(stats.Value().bytes));
                return Ok(Value::Map(std::move(map)));
        });
        builder.Method("convert", 2, 3, [archiveOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                auto format = ArgFormat(a[1].ToDisplayString(), "archive.convert");
                if (format.IsError())
                        return Err(format.Error());
                scriptlib::Options o(a, 2, "archive.convert");
                auto options = ReadWriteOptions(o);
                if (options.IsError())
                        return Err(options.Error());
                auto done = archiveOf(self).ConvertTo(a[0].ToDisplayString(), format.Value(), options.Value().first);
                if (done.IsError())
                        return Err(Fault(done.Error(), "archive.convert"));
                return Ok(Value::Boolean(done.Value()));
        });
        builder.Method("tree", 0, 2, [archiveOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                archive::Archive &ar = archiveOf(self);
                const int depth = a.size() > 1 ? int(std::clamp(a[1].AsInt64(), int64_t(1), int64_t(64))) : 64;
                archive::VirtualFs::NodeId from = archive::VirtualFs::ROOT;
                if (!a.empty() && !a[0].IsNil()) {
                        auto found = ar.Nav().Resolve(a[0].ToDisplayString());
                        if (found.IsNone())
                                return Err(Fail(String::Format("`archive.tree` : `%s` introuvable", a[0].ToDisplayString().CStr())));
                        from = found.Value();
                }
                return Ok(Value::Str(ar.Fs().Tree(from, depth)));
        });

        auto &type = *builder.type;
        type.mainThreadOnly = true;
        type.get = [archiveOf](const HostObject &self, const String &name) -> Option<Value> {
                // Nav() n'est pas const côté C++ : l'objet du script reste le seul
                // propriétaire, son état de navigation n'est jamais lu d'un autre fil.
                archive::Archive &ar = *As<ArchiveObject>(self).archive;
                if (name == "format")
                        return Some(Value::Str(archive::FormatName(ar.GetFormat())));
                if (name == "entries") {
                        auto list = std::make_shared<ListObject>();
                        const std::vector<archive::EntryInfo> &entries = ar.Entries();
                        list->items.reserve(entries.size());
                        for (size_t i = 0; i < entries.size(); ++i)
                                list->items.push_back(EntryToValue(entries[i], Some(i)));
                        return Some(Value::List(std::move(list)));
                }
                if (name == "encrypted")
                        return Some(Value::Boolean(ar.HasEncryptedEntries()));
                if (name == "skipped")
                        return Some(Value::UInt(ar.SkippedEntries()));
                if (name == "cwd")
                        return Some(Value::Str(ar.Nav().Cwd()));
                return NONE;
        };
        type.set = [](Interpreter &, const HostRef &self, const String &name,
                                  const Value &v) -> Result<bool, ScriptError> {
                if (name == "password") {
                        As<ArchiveObject>(self).archive->SetPassword(v.ToDisplayString());
                        return Ok(true);
                }
                return Ok(false);
        };
        type.display = [](const HostObject &self) {
                const archive::Archive &ar = *As<ArchiveObject>(self).archive;
                return String::Format("<archive.%s : %d entrée(s)>", archive::FormatName(ar.GetFormat()), int(ar.Entries().size()));
        };
}

// ============================================================================
// archive.writer
// ============================================================================

void DefineWriter(TypeBuilder &builder) {
        auto fsOf = [](const HostRef &self) -> archive::VirtualFs & { return *As<WriterObject>(self).fs; };
        auto find = [](archive::VirtualFs &fs, const Value &path, const char * /*fn*/) -> Option<archive::VirtualFs::NodeId> {
                auto normalized = archive::NormalizePath(path.ToDisplayString());
                if (normalized.IsError())
                        return NONE;
                return fs.Find(normalized.Value());
        };

        builder.Method("add_text", 2, 3, [fsOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                const String text = a[1].ToDisplayString();
                auto added = fsOf(self).AddFile(a[0].ToDisplayString(),
                                                                                archive::Bytes(text.CStr(), text.CStr() + text.GetSize()),
                                                                                a.size() > 2 ? a[2].AsInt64() : 0);
                if (added.IsError())
                        return Err(Fault(added.Error(), "archive.writer.add_text"));
                return Ok(Value::Host(self));
        });
        builder.Method("add_bytes", 2, 3, [fsOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                auto data = scriptlib::BytesOf(a[1], "archive.writer.add_bytes");
                if (data.IsError())
                        return Err(data.Error());
                auto added = fsOf(self).AddFile(a[0].ToDisplayString(), std::move(data).Unwrap(), a.size() > 2 ? a[2].AsInt64() : 0);
                if (added.IsError())
                        return Err(Fault(added.Error(), "archive.writer.add_bytes"));
                return Ok(Value::Host(self));
        });
        // add_file(chemin_archive, chemin_disque) : le contenu n'est lu qu'au
        // moment de l'écriture de l'archive (VirtualFs::AddDiskPath, non récursif).
        builder.Method("add_file", 2, 2, [fsOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                auto added = fsOf(self).AddDiskPath(a[1].ToDisplayString(), a[0].ToDisplayString(), false);
                if (added.IsError())
                        return Err(Fault(added.Error(), "archive.writer.add_file"));
                return Ok(Value::Host(self));
        });
        builder.Method("add_dir", 2, 3, [fsOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                scriptlib::Options o(a, 2, "archive.writer.add_dir");
                const bool recursive = o.Bool("recursive", true);
                const bool follow = o.Bool("follow_symlinks", false);
                if (auto error = o.Finish(); error.IsSome())
                        return Err(error.Unwrap());
                auto added = fsOf(self).AddDiskPath(a[1].ToDisplayString(), a[0].ToDisplayString(), recursive, follow);
                if (added.IsError())
                        return Err(Fault(added.Error(), "archive.writer.add_dir"));
                return Ok(Value::Int(int64_t(added.Value())));
        });
        builder.Method("mkdir", 1, 2, [fsOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                auto added = fsOf(self).AddDirectory(a[0].ToDisplayString(), a.size() > 1 ? a[1].AsInt64() : 0);
                if (added.IsError())
                        return Err(Fault(added.Error(), "archive.writer.mkdir"));
                return Ok(Value::Host(self));
        });
        builder.Method("symlink", 2, 2, [fsOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                auto added = fsOf(self).AddSymlink(a[0].ToDisplayString(), a[1].ToDisplayString());
                if (added.IsError())
                        return Err(Fault(added.Error(), "archive.writer.symlink"));
                return Ok(Value::Host(self));
        });
        builder.Method("remove", 1, 1, [fsOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                return Ok(Value::Boolean(fsOf(self).Remove(a[0].ToDisplayString())));
        });
        builder.Method("exists", 1, 1, [fsOf, find](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                return Ok(Value::Boolean(find(fsOf(self), a[0], "archive.writer.exists").IsSome()));
        });
        builder.Method("is_dir", 1, 1, [fsOf, find](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                auto id = find(fsOf(self), a[0], "archive.writer.is_dir");
                return Ok(Value::Boolean(id.IsSome() && fsOf(self).Get(id.Value()).IsDirectory()));
        });
        builder.Method("list", 0, 1, [fsOf, find](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                archive::VirtualFs &fs = fsOf(self);
                archive::VirtualFs::NodeId from = archive::VirtualFs::ROOT;
                if (!a.empty()) {
                        auto id = find(fs, a[0], "archive.writer.list");
                        if (id.IsNone())
                                return Ok(Value::Nil());
                        from = id.Value();
                }
                auto list = std::make_shared<ListObject>();
                for (archive::VirtualFs::NodeId child : fs.Children(from))
                        list->items.push_back(Value::Str(fs.PathOf(child)));
                return Ok(Value::List(std::move(list)));
        });
        builder.Method("size", 1, 1, [fsOf, find](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                archive::VirtualFs &fs = fsOf(self);
                auto id = find(fs, a[0], "archive.writer.size");
                if (id.IsNone() || !fs.Get(id.Value()).IsFile())
                        return Ok(Value::Nil());
                return Ok(Value::UInt(fs.Get(id.Value()).info.size));
        });
        builder.Method("count", 0, 0, [fsOf](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
                return Ok(Value::UInt(fsOf(self).FileCount()));
        });
        builder.Method("bytes", 0, 0, [fsOf](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
                return Ok(Value::UInt(fsOf(self).TotalSize()));
        });
        builder.Method("tree", 0, 1, [fsOf](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
                const int depth = a.empty() ? 64 : int(std::clamp(a[0].AsInt64(), int64_t(1), int64_t(64)));
                return Ok(Value::Str(fsOf(self).Tree(archive::VirtualFs::ROOT, depth)));
        });

        auto &type = *builder.type;
        type.mainThreadOnly = true;
        type.display = [](const HostObject &self) {
                const archive::VirtualFs &fs = *As<WriterObject>(self).fs;
                return String::Format("<archive.writer : %d fichier(s), %d octet(s)>", int(fs.FileCount()), int(fs.TotalSize()));
        };
}

// ============================================================================
// Fonctions de l'espace de noms
// ============================================================================

void InstallFunctions(Interpreter &vm, const std::shared_ptr<const HostType> &archiveType) {
        const String ns("archive");

        vm.RegisterNamespacedNative(ns, String("formats"), 0, 0, [](Interpreter &, Args &) -> Result<Value, ScriptError> {
                auto list = std::make_shared<ListObject>();
                for (int f = 0; f <= int(archive::Format::RAR); ++f)
                        list->items.push_back(Value::Str(archive::FormatName(archive::Format(f))));
                return Ok(Value::List(std::move(list)));
        });
        // archive.detect(chemin) : le format d'après les octets, nil sinon —
        // une sonde, jamais une erreur (un fichier ordinaire est une réponse).
        vm.RegisterNamespacedNative(ns, String("detect"), 1, 2, [](Interpreter &, Args &a) -> Result<Value, ScriptError> {
                scriptlib::Options o(a, 1, "archive.detect");
                auto options = ReadReadOptions(o);
                if (options.IsError())
                        return Err(options.Error());
                auto reader = archive::OpenArchiveFile(a[0].ToDisplayString(), options.Value());
                if (reader.IsError())
                        return Ok(Value::Nil());
                return Ok(Value::Str(archive::FormatName(reader.Value()->GetFormat())));
        });
        vm.RegisterNamespacedNative(
                ns, String("open"), 1, 2,
                [archiveType](Interpreter &, Args &a) -> Result<Value, ScriptError> {
                        scriptlib::Options o(a, 1, "archive.open");
                        auto options = ReadReadOptions(o);
                        if (options.IsError())
                                return Err(options.Error());
                        auto opened = archive::Archive::Open(a[0].ToDisplayString(), options.Value());
                        if (opened.IsError())
                                return Err(Fault(opened.Error(), "archive.open"));
                        auto object = std::make_shared<ArchiveObject>();
                        object->type = archiveType;
                        object->archive = std::make_shared<archive::Archive>(std::move(opened).Unwrap());
                        return Ok(Value::Host(std::move(object)));
                });
        vm.RegisterNamespacedNative(ns, String("writer"), 0, 0, [](Interpreter &, Args &) -> Result<Value, ScriptError> {
                auto object = std::make_shared<WriterObject>();
                object->fs = std::make_shared<archive::VirtualFs>();
                return Ok(Value::Host(std::move(object)));
        });
        vm.RegisterNamespacedNative(ns, String("write"), 2, 3, [](Interpreter &, Args &a) -> Result<Value, ScriptError> {
                if (!a[1].IsHost())
                        return Err(Fail(String::Format("`archive.write` : archive.writer attendu, trouvé `%s`", a[1].TypeName())));
                auto *writer = dynamic_cast<WriterObject *>(a[1].AsHost().get());
                if (!writer)
                        return Err(Fail(String("`archive.write` : archive.writer attendu")));
                scriptlib::Options o(a, 2, "archive.write");
                auto options = ReadWriteOptions(o);
                if (options.IsError())
                        return Err(options.Error());
                archive::Format format = options.Value().second.IsSome()
                                                 ? options.Value().second.Value()
                                                 : archive::Format::ZIP;
                if (options.Value().second.IsNone()) {
                        auto deduced = archive::FormatFromExtension(a[0].ToDisplayString());
                        if (deduced.IsNone())
                                return Err(Fail(String::Format("`archive.write` : format de %s inconnu — précisez l'option `format` "
                                                                                                                                   "ou une extension connue (zip, tar, 7z…)",
                                                               a[0].ToDisplayString().CStr())));
                        format = deduced.Value();
                }
                if (!archive::IsWritableFormat(format))
                        return Err(Fail(String::Format("`archive.write` : le format %s ne s'écrit pas (lecture seule)",
                                                       archive::FormatName(format))));
                auto written = archive::WriteArchiveFile(a[0].ToDisplayString(), format, *writer->fs, options.Value().first);
                if (written.IsError())
                        return Err(Fault(written.Error(), "archive.write"));
                return Ok(Value::Boolean(written.Value()));
        });
        vm.RegisterNamespacedNative(ns, String("pack"), 2, 3, [](Interpreter &, Args &a) -> Result<Value, ScriptError> {
                scriptlib::Options o(a, 2, "archive.pack");
                auto options = ReadWriteOptions(o);
                if (options.IsError())
                        return Err(options.Error());
                auto packed = archive::PackPath(a[0].ToDisplayString(), a[1].ToDisplayString(), options.Value().second,
                                                                                options.Value().first);
                if (packed.IsError())
                        return Err(Fault(packed.Error(), "archive.pack"));
                return Ok(Value::Int(int64_t(packed.Value())));
        });
}

} // namespace archlib

void InstallArchiveLibrary(Interpreter &vm) {
        using lib::TypeBuilder;
        TypeBuilder arch("archive.archive"), writer("archive.writer");
        archlib::DefineArchive(arch);
        archlib::DefineWriter(writer);
        auto archiveType = vm.RegisterHostType(String("archive"), String("archive"), arch.type);
        auto writerType = vm.RegisterHostType(String("archive"), String("writer"), writer.type);
        // archive.writer() : un système de fichiers virtuel vierge.
        vm.RegisterNamespacedNative(
                String("archive"), String("writer"), 0, 0,
                [writerType](Interpreter &, lib::Args &) -> Result<Value, ScriptError> {
                        auto object = std::make_shared<archlib::WriterObject>();
                        object->type = writerType;
                        object->fs = std::make_shared<archive::VirtualFs>();
                        return Ok(Value::Host(std::move(object)));
                });

        archlib::InstallFunctions(vm, archiveType);
}

} // namespace script
