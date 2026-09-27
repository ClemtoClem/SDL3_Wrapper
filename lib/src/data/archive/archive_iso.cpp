// Définitions de data/archive/archive_iso.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/archive/archive_iso.hpp"

namespace data::archive {

namespace detail::iso {

bool IsJolietEscape(const uint8_t* escape) noexcept {
	return escape[0] == '%' && escape[1] == '/' &&
		   (escape[2] == '@' || escape[2] == 'C' || escape[2] == 'E');
}

int64_t UnixFromRecordDate(const uint8_t* date) noexcept {
	if (date[1] == 0 || date[2] == 0)
		return 0;
	CivilTime civil;
	civil.year = 1900 + int(date[0]);
	civil.month = std::clamp<unsigned>(date[1], 1, 12);
	civil.day = std::clamp<unsigned>(date[2], 1, 31);
	civil.hour = std::min<unsigned>(date[3], 23);
	civil.minute = std::min<unsigned>(date[4], 59);
	civil.second = std::min<unsigned>(date[5], 59);
	return UnixFromCivil(civil) - int64_t(int8_t(date[6])) * 15 * 60;
}

void WriteRecordDate(BinaryWriter& writer, int64_t unix) {
	const CivilTime civil = CivilFromUnix(std::max<int64_t>(unix, 0));
	writer.U8(uint8_t(std::clamp<int64_t>(civil.year - 1900, 0, 255)));
	writer.U8(uint8_t(civil.month));
	writer.U8(uint8_t(civil.day));
	writer.U8(uint8_t(civil.hour));
	writer.U8(uint8_t(civil.minute));
	writer.U8(uint8_t(civil.second));
	writer.U8(0); // UTC
}

void WriteVolumeDate(BinaryWriter& writer, int64_t unix) {
	const CivilTime civil = CivilFromUnix(std::max<int64_t>(unix, 0));
	writer.WriteString(String::Format(
		"%04lld%02u%02u%02u%02u%02u00", static_cast<long long>(std::min<int64_t>(civil.year, 9999)),
		civil.month, civil.day, civil.hour, civil.minute, civil.second));
	writer.U8(0);
}

String CleanIsoName(String name) {
	size_t semicolon = name.Rfind(';');
	if (semicolon != String::NPOS)
		name = name.Substr(0, semicolon);
	if (name.EndsWith("."))
		name = name.Substr(0, name.GetSize() - 1);
	return name;
}

} // namespace detail::iso

// ── IsoReader ────────────────────────────────────────────────────────────────

Result<std::unique_ptr<IsoReader>, ArchiveError> IsoReader::Open(ArchiveSource source, const ReadOptions& options) {
	std::unique_ptr<IsoReader> reader(new IsoReader(std::move(source), options));
	if (auto error = reader->Index(); error.IsSome())
		return Err(error.Unwrap());
	return Ok(std::move(reader));
}

bool IsoReader::LooksLikeIso(sdl3::IOStream& stream) {
	auto bytes = ReadRange(stream, detail::iso::FIRST_DESCRIPTOR * detail::iso::SECTOR + 1, 5);
	return bytes.IsOk() && std::memcmp(bytes.Value().data(), "CD001", 5) == 0;
}

const std::vector<EntryInfo>& IsoReader::Entries() const noexcept {
	return m_entries;
}

const char* IsoReader::NamingScheme() const noexcept {
	return m_rockRidge ? "rock ridge" : (m_joliet ? "joliet" : "iso9660");
}

Result<ArchiveStream, ArchiveError> IsoReader::OpenEntry(size_t index) {
	if (index >= m_entries.size())
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT, String("entrée inexistante")));
	const EntryInfo& entry = m_entries[index];
	if (entry.type == EntryType::DIRECTORY)
		return OpenMemoryStream(Bytes());
	if (entry.type == EntryType::SYMLINK)
		return OpenMemoryStream(Bytes(entry.linkTarget.CStr(),
									  entry.linkTarget.CStr() + entry.linkTarget.GetSize()));
	// Une fenêtre par extent (fichiers > 4 Gio : plusieurs extents à la suite).
	const uint64_t imageSize = m_source.Size();
	std::vector<ArchiveStream> parts;
	for (const Extent& extent : m_extents[index]) {
		const uint64_t offset = extent.sector * detail::iso::SECTOR;
		if (offset > imageSize || extent.size > imageSize - offset)
			return Err(MakeError(ErrorKind::CORRUPT,
								 String::Format("%s : données hors de l'image (tronquée ?)",
												entry.path.CStr())));
		auto window = OpenSubStream(m_source.Stream(), offset, extent.size);
		if (window.IsError())
			return window;
		parts.push_back(std::move(window).Unwrap());
	}
	if (parts.size() == 1)
		return Ok(std::move(parts[0]));
	return OpenConcatStream(std::move(parts), Some(entry.size));
}

Option<ArchiveError> IsoReader::Index() {
	using namespace detail::iso;
	sdl3::IOStream& stream = m_source.Stream();
	Option<Bytes> primary = NONE, joliet = NONE;
	for (uint64_t sector = FIRST_DESCRIPTOR; sector < FIRST_DESCRIPTOR + 64; ++sector) {
		auto descriptor = ReadRange(stream, sector * SECTOR, SECTOR);
		if (descriptor.IsError() ||
			std::memcmp(descriptor.Value().data() + 1, "CD001", 5) != 0) {
			if (sector == FIRST_DESCRIPTOR)
				return Some(
					MakeError(ErrorKind::FORMAT, String("iso : descripteur de volume absent")));
			break;
		}
		const uint8_t type = descriptor.Value()[0];
		if (type == DESCRIPTOR_TERMINATOR)
			break;
		if (type == DESCRIPTOR_PRIMARY && primary.IsNone())
			primary = Some(std::move(descriptor).Unwrap());
		else if (type == DESCRIPTOR_SUPPLEMENTARY && joliet.IsNone() &&
				 IsJolietEscape(descriptor.Value().data() + 88))
			joliet = Some(std::move(descriptor).Unwrap());
	}
	if (primary.IsNone())
		return Some(MakeError(ErrorKind::FORMAT, String("iso : descripteur primaire absent")));

	// Champs du descripteur primaire, lus strictement dans les deux boutismes.
	{
		auto memory = ViewStream(primary.Value());
		if (memory.IsError())
			return Some(MakeError(ErrorKind::IO, memory.Error()));
		BinaryReader reader(memory.Value());
		(void)reader.Seek(40);
		m_volumeLabel = reader.ReadString(32).Trim();
		(void)reader.Seek(80);
		(void)reader.U32LeBe(); // taille du volume
		(void)reader.Seek(128);
		const uint16_t blockSize = reader.U16LeBe();
		if (!reader.Ok())
			return Some(MakeError(ErrorKind::CORRUPT,
								  String("iso : champs à double boutisme incohérents")));
		if (blockSize != SECTOR)
			return Some(
				MakeError(ErrorKind::UNSUPPORTED,
						  String::Format("iso : blocs de %u octets non gérés", blockSize)));
	}

	auto rootOf = [](const Bytes& descriptor) {
		Record root;
		root.sector = uint32_t(descriptor[158]) | (uint32_t(descriptor[159]) << 8) |
					  (uint32_t(descriptor[160]) << 16) | (uint32_t(descriptor[161]) << 24);
		root.size = uint32_t(descriptor[166]) | (uint32_t(descriptor[167]) << 8) |
					(uint32_t(descriptor[168]) << 16) | (uint32_t(descriptor[169]) << 24);
		root.flags = FLAG_DIRECTORY;
		return root;
	};

	// Rock Ridge : présence de « SP » dans l'enregistrement « . » de la racine.
	Record primaryRoot = rootOf(primary.Value());
	auto rootRecords = ReadDirectory(primaryRoot, false);
	if (rootRecords.IsError())
		return Some(rootRecords.Error());
	m_rockRidge = !rootRecords.Value().empty() && rootRecords.Value()[0].hasRockRidge;
	if (!m_rockRidge && joliet.IsSome()) {
		m_joliet = true;
		return Walk(rootOf(joliet.Value()), String(), true, 0);
	}
	return Walk(primaryRoot, String(), false, 0);
}

Option<ArchiveError> IsoReader::Walk(const Record& directory, const String& prefix, bool joliet, size_t depth) {
	using namespace detail::iso;
	if (depth > MAX_DEPTH)
		return Some(MakeError(ErrorKind::LIMIT, String("iso : arborescence trop profonde")));
	if (!m_visited.insert(directory.sector).second)
		return Some(MakeError(ErrorKind::CORRUPT, String("iso : boucle dans l'arborescence")));
	auto records = ReadDirectory(directory, joliet);
	if (records.IsError())
		return Some(records.Error());
	std::vector<Record>& list = records.Value();
	for (size_t i = 0; i < list.size(); ++i) {
		Record& record = list[i];
		if (record.self || record.parent || record.relocated)
			continue;
		// Fichier multi-extents : enregistrements successifs de même nom.
		std::vector<Extent> extents = {{record.sector, record.size}};
		while ((list[i].flags & FLAG_MULTI_EXTENT) && i + 1 < list.size() &&
			   list[i + 1].name == record.name) {
			++i;
			extents.push_back({list[i].sector, list[i].size});
		}
		String name =
			m_rockRidge && !record.rrName.IsEmpty() ? record.rrName : CleanIsoName(record.name);
		if (name.IsEmpty() || name == "." || name == ".." || name.Find('/') != String::NPOS)
			continue;
		EntryInfo entry;
		entry.path = prefix.IsEmpty() ? name : prefix + "/" + name;
		entry.modifiedTime = record.modifiedTime;
		entry.mode = record.mode.IsSome() ? (record.mode.Unwrap() & 07777) : 0;
		const bool isDirectory = (record.flags & FLAG_DIRECTORY) || record.childLink.IsSome();
		if (record.symlink) {
			entry.type = EntryType::SYMLINK;
			entry.linkTarget = record.linkTarget;
			entry.size = record.linkTarget.GetSize();
			entry.method = String("lien");
		} else if (isDirectory) {
			entry.type = EntryType::DIRECTORY;
			entry.method = String("stockée");
		} else {
			entry.type = EntryType::FILE;
			for (const Extent& extent : extents)
				entry.size += extent.size;
			entry.packedSize = entry.size;
			entry.method = String("stockée");
		}
		m_entries.push_back(entry);
		m_extents.push_back(isDirectory || record.symlink ? std::vector<Extent>{} : extents);
		if (isDirectory) {
			Record child = record;
			if (record.childLink.IsSome()) {
				// Dossier relogé : sa taille est celle de son propre « . ».
				child.sector = record.childLink.Unwrap();
				Record probe;
				probe.sector = child.sector;
				probe.size = SECTOR;
				auto self = ReadDirectory(probe, joliet);
				if (self.IsError() || self.Value().empty())
					return Some(MakeError(ErrorKind::CORRUPT,
										  String("iso : dossier relogé illisible")));
				child.size = self.Value()[0].size;
			}
			if (auto error = Walk(child, entry.path, joliet, depth + 1); error.IsSome())
				return error;
		}
	}
	return NONE;
}

Result<std::vector<IsoReader::Record>, ArchiveError> IsoReader::ReadDirectory(const Record& directory, bool joliet) {
	using namespace detail::iso;
	if (directory.size > (uint64_t(1) << 28))
		return Err(MakeError(ErrorKind::LIMIT, String("iso : répertoire démesuré")));
	auto data = ReadRange(m_source.Stream(), directory.sector * SECTOR, directory.size);
	if (data.IsError())
		return Err(
			MakeError(ErrorKind::CORRUPT,
					  String::Format("iso : répertoire illisible (%s)", data.Error().CStr())));
	auto memory = ViewStream(data.Value());
	if (memory.IsError())
		return Err(MakeError(ErrorKind::IO, memory.Error()));
	BinaryReader reader(memory.Value());
	std::vector<Record> records;
	while (reader.Remaining() > 0) {
		const uint64_t start = reader.Tell();
		const uint8_t length = reader.U8();
		if (length == 0) {
			// Fin du secteur : l'enregistrement suivant commence au secteur
			// d'après.
			const uint64_t next = (start / SECTOR + 1) * SECTOR;
			if (next >= reader.Size())
				break;
			(void)reader.Seek(next);
			continue;
		}
		if (length < RECORD_BASE + 1 || start + length > reader.Size())
			return Err(MakeError(ErrorKind::CORRUPT,
								 String("iso : enregistrement de répertoire invalide")));
		Record record;
		(void)reader.U8(); // attributs étendus
		record.sector = reader.U32Le();
		(void)reader.U32Be(); // copie gros-boutiste : les graveurs divergent
							  // parfois, le LE fait foi
		record.size = reader.U32Le();
		(void)reader.U32Be();
		uint8_t date[7];
		(void)reader.Read(date, 7);
		record.modifiedTime = UnixFromRecordDate(date);
		record.flags = reader.U8();
		(void)reader.Skip(6); // unité, intervalle, numéro de volume (LE+BE)
		const uint8_t nameLength = reader.U8();
		if (RECORD_BASE + nameLength > length)
			return Err(
				MakeError(ErrorKind::CORRUPT, String("iso : nom d'enregistrement tronqué")));
		Bytes rawName = reader.ReadBytes(nameLength);
		if (!reader.Ok())
			return Err(MakeError(ErrorKind::CORRUPT, String("iso : répertoire tronqué")));
		if (nameLength == 1 && (rawName[0] == 0 || rawName[0] == 1)) {
			record.self = rawName[0] == 0;
			record.parent = rawName[0] == 1;
		} else if (joliet) {
			record.name = Utf16ToUtf8(rawName, true);
		} else {
			record.name = String(reinterpret_cast<const char*>(rawName.data()), rawName.size());
		}
		// Zone d'utilisation système (Rock Ridge / SUSP).
		uint64_t systemUse = RECORD_BASE + nameLength + ((nameLength % 2 == 0) ? 1 : 0);
		if (!joliet && systemUse < length) {
			systemUse += (record.self && records.empty()) ? 0 : m_suspSkip;
			if (systemUse < length) {
				Bytes area(data.Value().begin() + std::ptrdiff_t(start + systemUse),
						   data.Value().begin() + std::ptrdiff_t(start + length));
				ParseSusp(area, record, records.empty() && record.self, 0);
			}
		}
		records.push_back(std::move(record));
		(void)reader.Seek(start + length);
	}
	return Ok(std::move(records));
}

void IsoReader::ParseSusp(const Bytes& area, Record& record, bool rootSelf, int hops) {
	using namespace detail::iso;
	auto memory = ViewStream(area);
	if (memory.IsError())
		return;
	BinaryReader reader(memory.Value());
	while (reader.Remaining() >= 4) {
		const uint64_t start = reader.Tell();
		uint8_t signature[2];
		(void)reader.Read(signature, 2);
		const uint8_t length = reader.U8();
		(void)reader.U8(); // version
		if (length < 4 || start + length > reader.Size())
			return;
		const auto is = [&signature](const char* tag) {
			return signature[0] == tag[0] && signature[1] == tag[1];
		};
		const size_t body = length - 4u;
		if (is("SP") && body >= 3) {
			(void)reader.Skip(2); // BE EF
			if (rootSelf)
				m_suspSkip = reader.U8();
			record.hasRockRidge = true;
		} else if (is("NM") && body >= 1) {
			const uint8_t flags = reader.U8();
			if (!(flags & 0x06)) // ni « . » ni « .. »
				record.rrName.Concat(reader.ReadString(body - 1));
			record.hasRockRidge = true;
		} else if (is("PX") && body >= 16) {
			record.mode = Some(reader.U32LeBe());
			record.symlink = record.symlink || (record.mode.Unwrap() & 0170000) == 0120000;
			record.hasRockRidge = true;
		} else if (is("SL") && body >= 1) {
			(void)reader.U8(); // drapeaux de l'entrée
			size_t consumed = 1;
			while (consumed + 2 <= body) {
				const uint8_t componentFlags = reader.U8();
				const uint8_t componentLength = reader.U8();
				consumed += 2;
				if (consumed + componentLength > body)
					break;
				String component = reader.ReadString(componentLength);
				consumed += componentLength;
				// Suite d'un composant coupé en deux : pas de séparateur.
				if (!record.linkContinues && !record.linkTarget.IsEmpty() &&
					!record.linkTarget.EndsWith("/"))
					record.linkTarget.Concat("/");
				if (componentFlags & 0x02)
					record.linkTarget.Concat(".");
				else if (componentFlags & 0x04)
					record.linkTarget.Concat("..");
				else if (componentFlags & 0x08)
					record.linkTarget = String("/");
				else
					record.linkTarget.Concat(component);
				record.linkContinues = (componentFlags & 0x01) != 0;
			}
			record.symlink = true;
			record.hasRockRidge = true;
		} else if (is("TF") && body >= 1) {
			const uint8_t flags = reader.U8();
			const bool longForm = (flags & 0x80) != 0;
			const size_t stampSize = longForm ? 17 : 7;
			for (int bit = 0; bit < 7; ++bit) {
				if (!(flags & (1 << bit)))
					continue;
				Bytes stamp = reader.ReadBytes(stampSize);
				if (!reader.Ok())
					break;
				if (bit == 1 && !longForm) // 0x02 = modification
					record.modifiedTime = UnixFromRecordDate(stamp.data());
			}
		} else if (is("CL") && body >= 8) {
			record.childLink = Some(uint64_t(reader.U32LeBe()));
		} else if (is("RE")) {
			record.relocated = true;
		} else if (is("CE") && body >= 24 && hops < 16) {
			const uint32_t block = reader.U32LeBe();
			const uint32_t offset = reader.U32LeBe();
			const uint32_t size = reader.U32LeBe();
			if (reader.Ok() && offset < SECTOR && size <= SECTOR - offset) {
				auto continuation =
					ReadRange(m_source.Stream(), uint64_t(block) * SECTOR + offset, size);
				if (continuation.IsOk()) {
					(void)reader.Seek(start + length);
					const uint64_t resume = reader.Tell();
					ParseSusp(continuation.Value(), record, rootSelf, hops + 1);
					(void)reader.Seek(resume);
					continue;
				}
			}
		} else if (is("ST")) {
			return;
		}
		if (!reader.Ok())
			return;
		(void)reader.Seek(start + length);
	}
}

// ── IsoWriter ────────────────────────────────────────────────────────────────

Result<bool, ArchiveError> IsoWriter::Write(const VirtualFs& fs, sdl3::IOStream& out, const WriteOptions& options) {
	using namespace detail::iso;
	if (options.encryption != Encryption::NONE)
		return Err(MakeError(ErrorKind::UNSUPPORTED,
							 String("iso : le format ne gère pas le chiffrement")));
	m_now = options.defaultTime != 0 ? options.defaultTime : CurrentUnixTime();
	m_fs = &fs;
	m_nodes.clear();
	m_continuation.clear();

	// ── Arbre et contenus ───────────────────────────────────────────────
	m_nodes.push_back(Node{});
	m_nodes[0].directory = true;
	m_nodes[0].modifiedTime = m_now;
	m_nodes[0].mode = 0755;
	if (auto error = Collect(VirtualFs::ROOT, 0, 0); error.IsSome())
		return Err(error.Unwrap());
	AssignIsoNames();

	// Dossiers en largeur (ordre des tables de chemins).
	std::vector<size_t> directories = {0};
	for (size_t d = 0; d < directories.size(); ++d)
		for (size_t child : m_nodes[directories[d]].children)
			if (m_nodes[child].directory)
				directories.push_back(child);
	for (size_t d = 0; d < directories.size(); ++d)
		m_nodes[directories[d]].directoryNumber = uint16_t(std::min<size_t>(d + 1, 0xFFFF));
	if (directories.size() > 0xFFFF)
		return Err(MakeError(ErrorKind::LIMIT, String("iso : plus de 65 535 dossiers")));

	// ── Plan des secteurs ───────────────────────────────────────────────
	// Les tailles ne dépendent pas des positions (champs de largeur fixe) :
	// une première passe les mesure, la seconde écrit avec les positions.
	const uint64_t pathPrimarySize = PathTable(directories, false, false).size();
	const uint64_t pathJolietSize = PathTable(directories, true, false).size();
	uint64_t sector = FIRST_DESCRIPTOR + 3; // primaire, Joliet, terminateur
	const uint64_t primaryL = sector;
	sector += SectorsFor(pathPrimarySize);
	const uint64_t primaryM = sector;
	sector += SectorsFor(pathPrimarySize);
	const uint64_t jolietL = sector;
	sector += SectorsFor(pathJolietSize);
	const uint64_t jolietM = sector;
	sector += SectorsFor(pathJolietSize);

	// Répertoires (Rock Ridge dans l'arbre primaire) ; la même passe mesure
	// la zone de continuation, régénérée à l'identique à l'émission.
	m_continuation.clear();
	for (size_t index : directories) {
		m_nodes[index].primarySize = DirectoryBytes(index, false).size();
		m_nodes[index].jolietSize = DirectoryBytes(index, true).size();
	}
	const uint64_t continuationSize = m_continuation.size();
	for (size_t index : directories) {
		m_nodes[index].primarySector = sector;
		sector += SectorsFor(m_nodes[index].primarySize);
	}
	for (size_t index : directories) {
		m_nodes[index].jolietSector = sector;
		sector += SectorsFor(m_nodes[index].jolietSize);
	}
	m_continuationSector = sector;
	sector += SectorsFor(continuationSize);
	for (Node& node : m_nodes) {
		if (node.directory || node.symlink)
			continue;
		node.dataSector = node.size == 0 ? 0 : sector;
		sector += SectorsFor(node.size);
	}
	const uint64_t totalSectors = sector;
	if (totalSectors > 0xFFFFFFFFu)
		return Err(MakeError(ErrorKind::LIMIT, String("iso : image trop grande")));

	// ── Émission ────────────────────────────────────────────────────────
	// Positions comptées : la sortie peut être séquentielle.
	auto counted = OpenCountingSink(out, std::make_shared<uint64_t>(0));
	if (counted.IsError())
		return Err(counted.Error());
	sdl3::IOStream& image = counted.Value().io;
	BinaryWriter writer(image);
	writer.Fill(FIRST_DESCRIPTOR * SECTOR);
	WriteDescriptor(writer, options, false, uint32_t(totalSectors), uint32_t(pathPrimarySize),
					uint32_t(primaryL), uint32_t(primaryM));
	WriteDescriptor(writer, options, true, uint32_t(totalSectors), uint32_t(pathJolietSize),
					uint32_t(jolietL), uint32_t(jolietM));
	writer.U8(DESCRIPTOR_TERMINATOR);
	writer.WriteString(String("CD001"));
	writer.U8(1);
	writer.Fill(SECTOR - 7);
	for (bool joliet : {false, true}) {
		for (bool bigEndian : {false, true}) {
			const Bytes bytes = PathTable(directories, joliet, bigEndian);
			writer.Write(bytes);
			writer.Fill(SectorsFor(bytes.size()) * SECTOR - bytes.size());
		}
	}
	m_continuation.clear();
	for (bool joliet : {false, true}) {
		for (size_t index : directories) {
			const Bytes bytes = DirectoryBytes(index, joliet);
			writer.Write(bytes);
			writer.Fill(SectorsFor(bytes.size()) * SECTOR - bytes.size());
		}
	}
	writer.Write(m_continuation);
	writer.Fill(SectorsFor(m_continuation.size()) * SECTOR - m_continuation.size());
	for (const Node& node : m_nodes) {
		if (node.directory || node.symlink)
			continue;
		if (node.source.IsSome()) {
			auto content = m_fs->OpenContent(node.source.Value());
			if (content.IsError())
				return Err(content.Error());
			auto copied = CopyStreamExactly(content.Value(), image, node.size,
											m_fs->Get(node.source.Value()).info.path);
			if (copied.IsError())
				return copied;
		} else {
			writer.Write(node.buffered);
		}
		writer.Fill(SectorsFor(node.size) * SECTOR - node.size);
	}
	if (!writer.Ok() || writer.Tell() != totalSectors * SECTOR)
		return Err(MakeError(ErrorKind::IO, String("iso : écriture impossible")));
	return Ok(true);
}

uint64_t IsoWriter::SectorsFor(uint64_t bytes) noexcept {
	return (bytes + detail::iso::SECTOR - 1) / detail::iso::SECTOR;
}

Option<ArchiveError> IsoWriter::Collect(VirtualFs::NodeId id, size_t parent, size_t depth) {
	if (depth > detail::iso::MAX_DEPTH)
		return Some(MakeError(ErrorKind::LIMIT, String("iso : arborescence trop profonde")));
	for (VirtualFs::NodeId childId : m_fs->Children(id)) {
		const VirtualFs::Node& source = m_fs->Get(childId);
		Node node;
		node.name = source.name;
		node.parent = parent;
		node.directory = source.IsDirectory();
		node.symlink = source.info.type == EntryType::SYMLINK;
		node.linkTarget = source.info.linkTarget;
		node.modifiedTime = source.info.modifiedTime != 0 ? source.info.modifiedTime : m_now;
		node.mode = source.info.mode != 0 ? source.info.mode : (node.directory ? 0755u : 0644u);
		if (source.IsFile()) {
			// Taille annoncée : le contenu est copié en flux à l'écriture ;
			// inconnue (0) : lu d'avance en mémoire.
			if (source.info.size == 0) {
				auto content = m_fs->OpenContent(childId);
				if (content.IsError())
					return Some(content.Error());
				auto bytes = ReadStreamToEnd(content.Value(), 0xFFFFF800u);
				if (bytes.IsError())
					return Some(bytes.Error());
				node.buffered = std::move(bytes).Unwrap();
				node.size = node.buffered.size();
			} else {
				node.source = Some(childId);
				node.size = source.info.size;
			}
			if (node.size >= 0xFFFFF800u)
				return Some(MakeError(ErrorKind::UNSUPPORTED,
									  String::Format("iso : %s dépasse 4 Gio (multi-extents "
													 "non gérés à l'écriture)",
													 source.info.path.CStr())));
		}
		const size_t index = m_nodes.size();
		m_nodes.push_back(std::move(node));
		m_nodes[parent].children.push_back(index);
		if (source.IsDirectory())
			if (auto error = Collect(childId, index, depth + 1); error.IsSome())
				return error;
	}
	return NONE;
}

void IsoWriter::AssignIsoNames() {
	for (Node& directory : m_nodes) {
		if (!directory.directory)
			continue;
		std::set<String> usedIso, usedJoliet;
		for (size_t child : directory.children) {
			Node& node = m_nodes[child];
			String stem = node.name, extension;
			size_t dot = node.name.Rfind('.');
			if (!node.directory && dot != String::NPOS && dot > 0) {
				stem = node.name.Substr(0, dot);
				extension = node.name.Substr(dot + 1);
			}
			auto sanitize = [](const String& text, size_t limit) {
				String out;
				for (size_t i = 0; i < text.GetSize() && out.GetSize() < limit; ++i) {
					char c = text.CharAt(i);
					if ((uint8_t(c) & 0xC0) == 0x80)
						continue; // octet de suite UTF-8 : un seul « _ » par caractère
					if (c >= 'a' && c <= 'z')
						c = char(c - 'a' + 'A');
					out.PushBack((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c : '_');
				}
				return out;
			};
			extension = sanitize(extension, 8);
			String candidateStem =
				sanitize(stem, 30 - extension.GetSize() - (extension.IsEmpty() ? 0 : 1));
			if (candidateStem.IsEmpty())
				candidateStem = String("_");
			String iso = extension.IsEmpty() ? candidateStem : candidateStem + "." + extension;
			for (int n = 1; usedIso.count(iso) != 0; ++n) {
				const String suffix = String::Format("~%d", n);
				const size_t room =
					30 - extension.GetSize() - (extension.IsEmpty() ? 0 : 1) - suffix.GetSize();
				String shortStem =
					candidateStem.Substr(0, std::min(room, candidateStem.GetSize())) + suffix;
				iso = extension.IsEmpty() ? shortStem : shortStem + "." + extension;
			}
			usedIso.insert(iso);
			node.isoName = iso;

			// Joliet : 103 unités UCS-2 au plus ; « * / : ; ? \ » interdits.
			String joliet;
			for (size_t i = 0; i < node.name.GetSize(); ++i) {
				const char c = node.name.CharAt(i);
				joliet.PushBack(
					c == '*' || c == '/' || c == ':' || c == ';' || c == '?' || c == '\\' ? '_'
																						  : c);
			}
			while (Utf8ToUtf16Be(joliet).size() / 2 > detail::iso::JOLIET_NAME_MAX)
				joliet = TruncateUtf8(joliet, joliet.GetSize() - 1);
			String uniqueJoliet = joliet;
			for (int n = 1; usedJoliet.count(uniqueJoliet) != 0; ++n)
				uniqueJoliet = TruncateUtf8(joliet, joliet.GetSize() > 8 ? joliet.GetSize() - 8
																		 : joliet.GetSize()) +
							   String::Format("~%d", n);
			usedJoliet.insert(uniqueJoliet);
			node.jolietName = uniqueJoliet;
		}
	}
	// Ordre des enregistrements : tri par nom ISO (resp. Joliet) — on trie
	// selon le nom ISO, et `DirectoryBytes` retrie pour Joliet.
	for (Node& directory : m_nodes)
		std::sort(directory.children.begin(), directory.children.end(),
				  [this](size_t a, size_t b) {
					  return m_nodes[a].isoName.Compare(m_nodes[b].isoName) < 0;
				  });
}

String IsoWriter::TruncateUtf8(const String& text, size_t bytes) {
	size_t cut = std::min(bytes, text.GetSize());
	while (cut > 0 && cut < text.GetSize() && (uint8_t(text.CharAt(cut)) & 0xC0) == 0x80)
		--cut;
	return text.Substr(0, cut);
}

Bytes IsoWriter::EncodedName(const Node& node, bool joliet) const {
	if (joliet)
		return Utf8ToUtf16Be(node.jolietName);
	String name = node.directory ? node.isoName : node.isoName + ";1";
	return Bytes(name.CStr(), name.CStr() + name.GetSize());
}

Bytes IsoWriter::PathTable(const std::vector<size_t>& directories, bool joliet, bool bigEndian) const {
	auto stream = CreateMemoryWriter();
	if (stream.IsError())
		return {};
	BinaryWriter writer(stream.Value());
	for (size_t index : directories) {
		const Node& node = m_nodes[index];
		const Bytes name = index == 0 ? Bytes{0} : EncodedName(node, joliet);
		const uint32_t location = uint32_t(joliet ? node.jolietSector : node.primarySector);
		writer.U8(uint8_t(name.size()));
		writer.U8(0);
		if (bigEndian) {
			writer.U32Be(location);
			writer.U16Be(m_nodes[node.parent].directoryNumber);
		} else {
			writer.U32Le(location);
			writer.U16Le(m_nodes[node.parent].directoryNumber);
		}
		writer.Write(name);
		if (name.size() % 2 != 0)
			writer.U8(0);
	}
	return stream.Value().DynamicMemoryBytes();
}

Bytes IsoWriter::DirectoryBytes(size_t index, bool joliet) {
	using namespace detail::iso;
	const Node& directory = m_nodes[index];
	std::vector<size_t> children;
	for (size_t child : directory.children)
		if (!(joliet && m_nodes[child].symlink))
			children.push_back(child);
	if (joliet)
		std::sort(children.begin(), children.end(), [this](size_t a, size_t b) {
			return Utf8ToUtf16Be(m_nodes[a].jolietName) < Utf8ToUtf16Be(m_nodes[b].jolietName);
		});

	Bytes out;
	auto append = [&out](const Bytes& record) {
		const uint64_t used = out.size() % SECTOR;
		if (used + record.size() > SECTOR)
			out.resize(out.size() + (SECTOR - used),
					   0); // un enregistrement ne chevauche pas deux secteurs
		out.insert(out.end(), record.begin(), record.end());
	};
	append(Record(index, Bytes{0}, joliet, index == 0 ? 1 : 2));
	append(Record(directory.parent, Bytes{1}, joliet, 2));
	for (size_t child : children)
		append(Record(child, EncodedName(m_nodes[child], joliet), joliet, 0));
	return out;
}

Bytes IsoWriter::Record(size_t index, const Bytes& name, bool joliet, int kind) {
	using namespace detail::iso;
	const Node& node = m_nodes[index];
	Bytes susp = joliet ? Bytes() : SystemUse(node, kind);
	const size_t fixed = RECORD_BASE + name.size() + (name.size() % 2 == 0 ? 1 : 0);
	if (fixed + susp.size() > 255) {
		// Débordement : entrées reportées dans la zone de continuation, CE en
		// ligne.
		const size_t inlineRoom = 255 - fixed - 28;
		size_t cut = 0;
		while (cut < susp.size() && cut + susp[cut + 2] <= inlineRoom)
			cut += susp[cut + 2];
		Bytes rest(susp.begin() + std::ptrdiff_t(cut), susp.end());
		susp.resize(cut);
		if (m_continuation.size() % SECTOR + rest.size() > SECTOR)
			m_continuation.resize((m_continuation.size() / SECTOR + 1) * SECTOR, 0);
		const uint64_t offset = m_continuation.size();
		m_continuation.insert(m_continuation.end(), rest.begin(), rest.end());
		auto stream = CreateMemoryWriter();
		if (stream.IsOk()) {
			BinaryWriter ce(stream.Value());
			ce.WriteString(String("CE"));
			ce.U8(28);
			ce.U8(1);
			ce.U32LeBe(uint32_t(m_continuationSector + offset / SECTOR));
			ce.U32LeBe(uint32_t(offset % SECTOR));
			ce.U32LeBe(uint32_t(rest.size()));
			Bytes entry = stream.Value().DynamicMemoryBytes();
			susp.insert(susp.end(), entry.begin(), entry.end());
		}
	}

	auto stream = CreateMemoryWriter();
	if (stream.IsError())
		return {};
	BinaryWriter writer(stream.Value());
	const uint64_t sector =
		node.directory ? (joliet ? node.jolietSector : node.primarySector) : node.dataSector;
	const uint64_t size = node.directory ? (joliet ? node.jolietSize : node.primarySize)
										 : (node.symlink ? 0 : node.size);
	writer.U8(uint8_t(fixed + susp.size()));
	writer.U8(0);
	writer.U32LeBe(uint32_t(sector));
	writer.U32LeBe(uint32_t(node.directory ? SectorsFor(size) * SECTOR
										   : size)); // dossier : secteurs entiers
	WriteRecordDate(writer, node.modifiedTime);
	writer.U8(node.directory ? FLAG_DIRECTORY : 0);
	writer.U8(0);
	writer.U8(0);
	writer.U16LeBe(1);
	writer.U8(uint8_t(name.size()));
	writer.Write(name);
	if (name.size() % 2 == 0)
		writer.U8(0);
	writer.Write(susp);
	return stream.Value().DynamicMemoryBytes();
}

Bytes IsoWriter::SystemUse(const Node& node, int kind) const {
	auto stream = CreateMemoryWriter();
	if (stream.IsError())
		return {};
	BinaryWriter writer(stream.Value());
	auto header = [&writer](const char* tag, size_t length) {
		writer.WriteString(String(tag));
		writer.U8(uint8_t(length));
		writer.U8(1);
	};
	if (kind == 1) {
		header("SP", 7);
		writer.U8(0xBE);
		writer.U8(0xEF);
		writer.U8(0);
		static constexpr const char* ID = "RRIP_1991A";
		header("ER", 8 + 10);
		writer.U8(10);
		writer.U8(0);
		writer.U8(0);
		writer.U8(1);
		writer.WriteString(String(ID));
	}
	const uint32_t type = node.directory ? 0040000u : (node.symlink ? 0120000u : 0100000u);
	header("PX", 36);
	writer.U32LeBe(type | (node.mode & 07777));
	writer.U32LeBe(node.directory ? 2 : 1);
	writer.U32LeBe(0);
	writer.U32LeBe(0);
	header("TF", 5 + 7);
	writer.U8(0x02);
	detail::iso::WriteRecordDate(writer, node.modifiedTime);
	if (kind == 0) {
		// NM (morceaux de 250 octets au plus, drapeau « suite »).
		for (size_t at = 0; at < node.name.GetSize() || at == 0; at += 250) {
			const size_t part = std::min<size_t>(250, node.name.GetSize() - at);
			const bool more = at + part < node.name.GetSize();
			header("NM", 5 + part);
			writer.U8(more ? 0x01 : 0x00);
			writer.WriteString(node.name.Substr(at, part));
			if (!more)
				break;
		}
		if (node.symlink) {
			Bytes components;
			String target = node.linkTarget;
			if (target.StartsWith("/")) {
				components.push_back(0x08);
				components.push_back(0);
			}
			for (const String& part : target.Split('/')) {
				if (part.IsEmpty())
					continue;
				if (part == ".") {
					components.push_back(0x02);
					components.push_back(0);
				} else if (part == "..") {
					components.push_back(0x04);
					components.push_back(0);
				} else {
					const size_t length = std::min<size_t>(part.GetSize(), 240);
					components.push_back(0);
					components.push_back(uint8_t(length));
					components.insert(components.end(), part.CStr(), part.CStr() + length);
				}
			}
			// Découpe en entrées SL de 250 octets au plus, sur des frontières de
			// composants.
			size_t at = 0;
			while (at < components.size()) {
				size_t end = at;
				while (end < components.size() && end - at + 2 + components[end + 1] <= 250)
					end += 2 + components[end + 1];
				header("SL", 5 + (end - at));
				writer.U8(end < components.size() ? 0x01 : 0x00);
				writer.Write(components.data() + at, end - at);
				at = end;
			}
		}
	}
	return stream.Value().DynamicMemoryBytes();
}

void IsoWriter::WriteDescriptor(BinaryWriter& writer, const WriteOptions& options, bool joliet,
		uint32_t totalSectors, uint32_t pathTableSize, uint32_t pathL,
		uint32_t pathM) {
	using namespace detail::iso;
	auto text = [&writer, joliet](const String& value, size_t width) {
		if (!joliet) {
			writer.WriteFixed(value, width, ' ');
			return;
		}
		Bytes utf16 = Utf8ToUtf16Be(value);
		if (utf16.size() > width)
			utf16.resize(width & ~size_t(1));
		writer.Write(utf16);
		for (size_t i = utf16.size(); i + 1 < width; i += 2) {
			writer.U8(0);
			writer.U8(' ');
		}
		if ((width - utf16.size()) % 2 != 0)
			writer.U8(0);
	};
	String label;
	for (size_t i = 0; i < options.volumeLabel.GetSize() && label.GetSize() < 32; ++i) {
		char c = options.volumeLabel.CharAt(i);
		if ((uint8_t(c) & 0xC0) == 0x80)
			continue;
		if (c >= 'a' && c <= 'z')
			c = char(c - 'a' + 'A');
		label.PushBack((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c : '_');
	}
	const uint64_t start = writer.Tell();
	writer.U8(joliet ? DESCRIPTOR_SUPPLEMENTARY : DESCRIPTOR_PRIMARY);
	writer.WriteString(String("CD001"));
	writer.U8(1);
	writer.U8(0);
	text(String(), 32);								// système
	text(joliet ? options.volumeLabel : label, 32); // volume
	writer.Fill(8);
	writer.U32LeBe(totalSectors);
	if (joliet) {
		writer.WriteString(String("%/E")); // Joliet niveau 3
		writer.Fill(29);
	} else {
		writer.Fill(32);
	}
	writer.U16LeBe(1); // taille de l'ensemble de volumes
	writer.U16LeBe(1); // numéro de séquence
	writer.U16LeBe(uint16_t(SECTOR));
	writer.U32LeBe(pathTableSize);
	writer.U32Le(pathL);
	writer.U32Le(0);
	writer.U32Be(pathM);
	writer.U32Be(0);
	const Bytes root = RootRecord(joliet);
	writer.Write(root);
	text(String(), 128);							 // ensemble de volumes
	text(String(), 128);							 // éditeur
	text(String(), 128);							 // préparateur
	text(String("SDL3_WRAPPER DATA::ARCHIVE"), 128); // application
	text(String(), 37);
	text(String(), 37);
	text(String(), 37);
	WriteVolumeDate(writer, m_now);
	WriteVolumeDate(writer, m_now);
	writer.WriteString(String("0000000000000000"));
	writer.U8(0);
	writer.WriteString(String("0000000000000000"));
	writer.U8(0);
	writer.U8(1); // version de structure
	writer.Fill(SECTOR - (writer.Tell() - start));
}

Bytes IsoWriter::RootRecord(bool joliet) const {
	using namespace detail::iso;
	const Node& root = m_nodes[0];
	auto stream = CreateMemoryWriter();
	if (stream.IsError())
		return {};
	BinaryWriter writer(stream.Value());
	writer.U8(34);
	writer.U8(0);
	writer.U32LeBe(uint32_t(joliet ? root.jolietSector : root.primarySector));
	writer.U32LeBe(uint32_t(SectorsFor(joliet ? root.jolietSize : root.primarySize) * SECTOR));
	WriteRecordDate(writer, root.modifiedTime);
	writer.U8(FLAG_DIRECTORY);
	writer.U8(0);
	writer.U8(0);
	writer.U16LeBe(1);
	writer.U8(1);
	writer.U8(0);
	return stream.Value().DynamicMemoryBytes();
}

} // namespace data::archive
