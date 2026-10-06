// Définitions de data/archive/archive_tar.hpp
#include "data/archive/archive_tar.hpp"

namespace data::archive {

namespace detail::tar {

String Field(const Header& header, size_t offset, size_t size) {
	size_t length = 0;
	while (length < size && header[offset + length] != 0)
		++length;
	return String(reinterpret_cast<const char*>(header.data() + offset), length);
}

Option<uint64_t> Number(const Header& header, size_t offset, size_t size) {
	if (header[offset] & 0x80) {
		if (header[offset] & 0x40)
			return NONE; // négatif : refusé
		uint64_t value = header[offset] & 0x3F;
		for (size_t i = 1; i < size; ++i) {
			if (value >> 56)
				return NONE;
			value = (value << 8) | header[offset + i];
		}
		return Some(value);
	}
	size_t i = 0;
	while (i < size && header[offset + i] == ' ')
		++i;
	uint64_t value = 0;
	bool digits = false;
	for (; i < size; ++i) {
		const uint8_t c = header[offset + i];
		if (c == 0 || c == ' ')
			break;
		if (c < '0' || c > '7' || (value >> 61))
			return NONE;
		value = value * 8 + (c - '0');
		digits = true;
	}
	return digits || i > 0 || header[offset] == 0 ? Some(value) : Option<uint64_t>(NONE);
}

bool IsZeroBlock(const Header& header) noexcept {
	return std::all_of(header.begin(), header.end(), [](uint8_t b) { return b == 0; });
}

bool ChecksumMatches(const Header& header) {
	Option<uint64_t> stored = Number(header, CHECKSUM, CHECKSUM_SIZE);
	if (stored.IsNone())
		return false;
	int64_t unsignedSum = 0, signedSum = 0;
	for (size_t i = 0; i < BLOCK; ++i) {
		const bool inField = i >= CHECKSUM && i < CHECKSUM + CHECKSUM_SIZE;
		unsignedSum += inField ? ' ' : header[i];
		signedSum += inField ? ' ' : int8_t(header[i]);
	}
	return uint64_t(unsignedSum) == stored.Unwrap() || uint64_t(signedSum) == stored.Unwrap();
}

bool LooksLikeTar(std::span<const uint8_t> bytes) {
	if (bytes.size() < BLOCK)
		return false;
	Header header;
	std::memcpy(header.data(), bytes.data(), BLOCK);
	return !IsZeroBlock(header) && ChecksumMatches(header);
}

std::vector<std::pair<String, String>> ParsePax(const Bytes& data) {
	std::vector<std::pair<String, String>> records;
	size_t at = 0;
	while (at < data.size()) {
		size_t length = 0, cursor = at;
		while (cursor < data.size() && data[cursor] >= '0' && data[cursor] <= '9' &&
			   length < data.size())
			length = length * 10 + (data[cursor++] - '0');
		if (cursor >= data.size() || data[cursor] != ' ' || length == 0 ||
			length > data.size() - at)
			break;
		size_t recordEnd = at + length;
		size_t equals = cursor + 1;
		while (equals < recordEnd && data[equals] != '=')
			++equals;
		if (equals >= recordEnd || data[recordEnd - 1] != '\n')
			break;
		records.emplace_back(
			String(reinterpret_cast<const char*>(data.data() + cursor + 1), equals - cursor - 1),
			String(reinterpret_cast<const char*>(data.data() + equals + 1),
				   recordEnd - 1 - equals - 1));
		at = recordEnd;
	}
	return records;
}

Option<uint64_t> ParseDecimal(const String& text) {
	uint64_t value = 0;
	size_t i = 0;
	for (; i < text.GetSize() && text.CharAt(i) != '.'; ++i) {
		const char c = text.CharAt(i);
		if (c < '0' || c > '9' || value > (UINT64_MAX - 9) / 10)
			return NONE;
		value = value * 10 + uint64_t(c - '0');
	}
	return i == 0 ? Option<uint64_t>(NONE) : Some(value);
}

// ── SparseStreamImpl ─────────────────────────────────────────────────────────

Result<size_t, ArchiveError> SparseStreamImpl::Produce(uint8_t* out, size_t max) {
	while (m_segment < m_segments.size() &&
		   m_offset >= m_segments[m_segment].offset + m_segments[m_segment].size)
		++m_segment;
	if (m_offset >= m_size)
		return Ok(size_t(0));
	uint64_t take = std::min<uint64_t>(max, m_size - m_offset);
	if (m_segment < m_segments.size() && m_offset >= m_segments[m_segment].offset) {
		const SparseSegment& segment = m_segments[m_segment];
		take = std::min(take, segment.offset + segment.size - m_offset);
		auto got = StreamRead(m_data, out, size_t(take));
		if (got.IsError())
			return got;
		if (got.Value() != take)
			return Err(
				MakeError(ErrorKind::CORRUPT, String("données de fichier creux tronquées")));
	} else {
		if (m_segment < m_segments.size())
			take = std::min(take, m_segments[m_segment].offset - m_offset);
		std::memset(out, 0, size_t(take));
	}
	m_offset += take;
	return Ok(size_t(take));
}

bool SparseStreamImpl::Restart() {
	if (m_data.io.Seek(0, SDL_IO_SEEK_SET) != 0)
		return false;
	m_offset = 0;
	m_segment = 0;
	return true;
}

const char* ContainerMethod(Format format) noexcept {
	switch (format) {
	case Format::TAR_GZIP:
		return "deflate";
	case Format::TAR_XZ:
		return "lzma2";
	case Format::TAR_BZIP2:
		return "bzip2";
	case Format::TAR_ZSTD:
		return "zstd";
	default:
		return "stockée";
	}
}

} // namespace detail::tar

// ── TarReader ────────────────────────────────────────────────────────────────

Result<std::unique_ptr<TarReader>, ArchiveError> TarReader::Open(ArchiveSource source, const ReadOptions& options) {
	std::unique_ptr<TarReader> reader(new TarReader(std::move(source), options, Format::TAR));
	if (auto error = reader->Index(); error.IsSome())
		return Err(error.Unwrap());
	return Ok(std::move(reader));
}

Result<std::unique_ptr<TarReader>, ArchiveError> TarReader::OpenCompressed(ArchiveSource source, const ReadOptions& options) {
	auto head = ReadRange(source.Stream(), 0, std::min<uint64_t>(source.Size(), 16));
	if (head.IsError())
		return Err(MakeError(ErrorKind::IO, head.Error()));
	const Option<Format> compression = DetectCompression(head.Value());
	if (compression.IsNone())
		return Err(MakeError(ErrorKind::FORMAT,
							 String("tar compressé : ni gzip, ni xz, ni bzip2, ni zstd")));
	std::unique_ptr<TarReader> reader(
		new TarReader(std::move(source), options, TarFormatOf(compression.Unwrap())));
	if (auto error = reader->OpenDecoded(); error.IsSome())
		return Err(error.Unwrap());
	if (auto error = reader->Index(); error.IsSome())
		return Err(error.Unwrap());
	return Ok(std::move(reader));
}

const std::vector<EntryInfo>& TarReader::Entries() const noexcept {
	return m_entries;
}

Result<ArchiveStream, ArchiveError> TarReader::OpenEntry(size_t index) {
	return OpenFollowing(index, 0);
}

sdl3::IOStream& TarReader::TarStream() noexcept {
	return m_decoded.IsSome() ? m_decoded.Value().io : m_source.Stream();
}

Result<ArchiveStream, ArchiveError> TarReader::OpenDecompressor() {
	auto window = OpenSubStream(m_source.Stream(), 0, m_source.Size());
	if (window.IsError())
		return window;
	return data::archive::OpenDecompressor(std::move(window).Unwrap(), CompressionOf(m_format));
}

Option<ArchiveError> TarReader::OpenDecoded() {
	auto decoder = OpenDecompressor();
	if (decoder.IsError())
		return Some(decoder.Error());
	auto cached = ReadStreamToEnd(decoder.Value(), m_options.memoryCacheLimit);
	if (cached.IsOk()) {
		auto memory = OpenMemoryStream(std::move(cached).Unwrap());
		if (memory.IsError())
			return Some(memory.Error());
		m_decoded = Some(std::move(memory).Unwrap());
		m_cached = true;
		return NONE;
	}
	if (cached.Error().kind != ErrorKind::LIMIT)
		return Some(cached.Error());
	decoder = OpenDecompressor();
	if (decoder.IsError())
		return Some(decoder.Error());
	m_decoded = Some(std::move(decoder).Unwrap());
	return NONE;
}

ArchiveError TarReader::StreamFailure(const char* fallback) const {
	if (m_decoded.IsSome() && m_decoded.Value().state &&
		m_decoded.Value().state->error.IsSome())
		return m_decoded.Value().state->error.Value();
	return MakeError(ErrorKind::CORRUPT, String(fallback));
}

Result<ArchiveStream, ArchiveError> TarReader::OpenFollowing(size_t index, int depth) {
	if (index >= m_entries.size())
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT, String("entrée inexistante")));
	const EntryInfo& entry = m_entries[index];
	const Record& record = m_records[index];
	if (entry.type == EntryType::DIRECTORY)
		return OpenMemoryStream(Bytes());
	if (entry.type == EntryType::SYMLINK)
		return OpenMemoryStream(Bytes(entry.linkTarget.CStr(),
									  entry.linkTarget.CStr() + entry.linkTarget.GetSize()));
	if (record.hardLink) {
		// Lien physique : contenu de la DERNIÈRE entrée de ce chemin qui le
		// précède.
		if (depth > 16)
			return Err(
				MakeError(ErrorKind::CORRUPT,
						  String::Format("%s : boucle de liens physiques", entry.path.CStr())));
		for (size_t i = index; i-- > 0;)
			if (m_entries[i].path == entry.linkTarget)
				return OpenFollowing(i, depth + 1);
		return Err(MakeError(ErrorKind::CORRUPT,
							 String::Format("%s : cible du lien physique « %s » absente",
											entry.path.CStr(), entry.linkTarget.CStr())));
	}
	auto data = OpenSubStream(TarStream(), record.dataOffset, record.storedSize);
	if (data.IsError())
		return data;
	if (!record.sparse)
		return OpenCheckedStream(std::move(data).Unwrap(), Some(entry.size), NONE, entry.path);
	// Fichier creux : carte vérifiée (triée, dans la taille réelle, tenant
	// dans les données présentes).
	uint64_t previousEnd = 0, stored = 0;
	for (const detail::tar::SparseSegment& segment : record.segments) {
		if (segment.offset < previousEnd || segment.offset > entry.size ||
			segment.size > entry.size - segment.offset ||
			segment.size > record.storedSize - stored)
			return Err(MakeError(
				ErrorKind::CORRUPT,
				String::Format("%s : carte de fichier creux incohérente", entry.path.CStr())));
		previousEnd = segment.offset + segment.size;
		stored += segment.size;
	}
	auto state = std::make_shared<StreamState>();
	auto sparse = MakeStream(std::make_unique<detail::tar::SparseStreamImpl>(
								 std::move(data).Unwrap(), record.segments, entry.size, state),
							 state);
	if (sparse.IsError())
		return sparse;
	return OpenCheckedStream(std::move(sparse).Unwrap(), Some(entry.size), NONE, entry.path);
}

Option<ArchiveError> TarReader::Index() {
	using namespace detail::tar;
	sdl3::IOStream& stream = TarStream();
	BinaryReader reader(stream);
	(void)reader.Seek(0); // la source a pu être lue ailleurs (détection du format)
	// Taille inconnue (flux de décompression) : les troncatures se voient
	// en avançant.
	const Sint64 knownTotal = stream.GetSize();
	const uint64_t total = knownTotal < 0 ? UINT64_MAX : uint64_t(knownTotal);
	bool truncatedTail = false; // bourrage du dernier bloc absent

	String longName, longLink;
	std::vector<std::pair<String, String>> globalPax, localPax;
	bool pendingExtension = false;
	bool first = true;

	while (!truncatedTail) {
		Header header;
		size_t got = 0;
		while (got < BLOCK) {
			const size_t done = stream.Read(header.data() + got, BLOCK - got);
			if (done == 0)
				break;
			got += done;
		}
		if (got == 0 && stream.Status() == sdl3::IOStatus::ERROR)
			return Some(StreamFailure("tar : lecture impossible"));
		if (got < BLOCK) {
			if (got == 0 || !first)
				break; // fin sans blocs nuls (tolérée) ou reste d'enregistrement
			return Some(MakeError(ErrorKind::FORMAT, String("tar : fichier trop court")));
		}
		if (IsZeroBlock(header))
			break; // fin d'archive (un seul bloc nul toléré)
		if (!ChecksumMatches(header))
			return Some(MakeError(
				first ? ErrorKind::FORMAT : ErrorKind::CORRUPT,
				String::Format("tar : somme de contrôle incorrecte à l'octet %llu",
							   static_cast<unsigned long long>(reader.Tell() - BLOCK))));
		first = false;
		const char type = char(header[TYPE]);
		Option<uint64_t> sizeField = Number(header, SIZE, NUMBER12_SIZE);
		if (sizeField.IsNone())
			return Some(MakeError(ErrorKind::CORRUPT, String("tar : taille illisible")));
		uint64_t size = sizeField.Unwrap();
		// Les liens et dossiers n'ont pas de données, quelle que soit la taille
		// notée.
		const bool hasData = !(type == '1' || type == '2' || type == '5' || type == '3' ||
							   type == '4' || type == '6');

		// ── En-têtes d'extension ─────────────────────────────────────────
		if (type == 'L' || type == 'K' || type == 'x' || type == 'g') {
			if (size > (uint64_t(1) << 24))
				return Some(
					MakeError(ErrorKind::LIMIT, String("tar : en-tête d'extension démesuré")));
			const uint64_t dataStart = reader.Tell();
			Bytes data = reader.ReadBytes(size);
			(void)reader.Seek(dataStart + ((size + BLOCK - 1) / BLOCK) * BLOCK);
			if (!reader.Ok())
				return Some(StreamFailure("tar : en-tête d'extension tronqué"));
			if (type == 'L' || type == 'K') {
				while (!data.empty() && data.back() == 0)
					data.pop_back();
				String text(reinterpret_cast<const char*>(data.data()), data.size());
				(type == 'L' ? longName : longLink) = text;
			} else {
				auto records = ParsePax(data);
				// pax GNU.sparse 0.0 : paires offset/numbytes répétées, converties
				// en carte 0.1 avant la fusion (qui ne garde que la dernière valeur).
				String sparsePairs;
				for (const auto& [key, value] : records)
					if (key == "GNU.sparse.offset" || key == "GNU.sparse.numbytes")
						sparsePairs.Concat(sparsePairs.IsEmpty() ? value : "," + value);
				if (!sparsePairs.IsEmpty())
					records.emplace_back(String("GNU.sparse.map"), sparsePairs);
				auto& target = type == 'x' ? localPax : globalPax;
				for (auto& record : records) {
					auto existing =
						std::find_if(target.begin(), target.end(), [&record](const auto& kv) {
							return kv.first == record.first;
						});
					if (existing != target.end())
						existing->second = record.second;
					else
						target.push_back(record);
				}
			}
			pendingExtension = true;
			continue;
		}

		// ── Entrée ordinaire ─────────────────────────────────────────────
		EntryInfo entry;
		const bool ustar = std::memcmp(header.data() + MAGIC, "ustar", 5) == 0;
		String name = Field(header, NAME, NAME_SIZE);
		if (ustar && header[PREFIX] != 0 &&
			std::memcmp(header.data() + MAGIC, "ustar  ", 7) != 0)
			name =
				Field(header, PREFIX, PREFIX_SIZE) + "/" + name; // pas pour l'ancien format GNU
		String link = Field(header, LINK, LINK_SIZE);
		entry.modifiedTime = int64_t(Number(header, MTIME, NUMBER12_SIZE).UnwrapOr(0));
		entry.mode = uint32_t(Number(header, MODE, ID_SIZE).UnwrapOr(0) & 07777);
		if (!longName.IsEmpty())
			name = longName;
		if (!longLink.IsEmpty())
			link = longLink;
		int sparseMajor = 0;
		String sparseMap;
		Option<uint64_t> sparseRealSize = NONE;
		String sparseName;
		auto applyPax = [&](const std::vector<std::pair<String, String>>& records) {
			for (const auto& [key, value] : records) {
				if (key == "GNU.sparse.major")
					sparseMajor = int(ParseDecimal(value).UnwrapOr(0));
				else if (key == "GNU.sparse.map")
					sparseMap = value;
				else if (key == "GNU.sparse.realsize" || key == "GNU.sparse.size")
					sparseRealSize = ParseDecimal(value);
				else if (key == "GNU.sparse.name")
					sparseName = value;
				else if (key == "path")
					name = value;
				else if (key == "linkpath")
					link = value;
				else if (key == "size")
					size = ParseDecimal(value).UnwrapOr(size);
				else if (key == "mtime")
					entry.modifiedTime =
						int64_t(ParseDecimal(value).UnwrapOr(uint64_t(entry.modifiedTime)));
			}
		};
		applyPax(globalPax);
		applyPax(localPax);
		if (!sparseName.IsEmpty())
			name = sparseName; // le nom ustar est « GNUSparseFile.0/… »
		localPax.clear();
		longName.Clear();
		longLink.Clear();
		pendingExtension = false;

		Record record;
		record.type = type;
		uint64_t realSize = size;
		if (type == 'S') {
			// Carte dans l'en-tête, suite dans des blocs d'extension.
			record.sparse = true;
			auto readMap = [&record](const Header& block, size_t offset, size_t count) {
				for (size_t k = 0; k < count; ++k) {
					Option<uint64_t> at = Number(block, offset + 24 * k, NUMBER12_SIZE);
					Option<uint64_t> length =
						Number(block, offset + 24 * k + 12, NUMBER12_SIZE);
					if (at.IsNone() || length.IsNone())
						return false;
					if (block[offset + 24 * k] != 0 &&
						record.segments.size() < MAX_SPARSE_SEGMENTS)
						record.segments.push_back({at.Unwrap(), length.Unwrap()});
				}
				return true;
			};
			if (!readMap(header, GNU_SPARSE, GNU_SPARSE_ENTRIES))
				return Some(MakeError(ErrorKind::CORRUPT,
									  String("tar : carte de fichier creux illisible")));
			realSize = Number(header, GNU_REAL_SIZE, NUMBER12_SIZE).UnwrapOr(size);
			bool extended = header[GNU_IS_EXTENDED] != 0;
			while (extended) {
				Header extension;
				if (!reader.Read(extension.data(), BLOCK) ||
					!readMap(extension, 0, GNU_EXTENSION_ENTRIES))
					return Some(MakeError(ErrorKind::CORRUPT,
										  String("tar : extension de fichier creux tronquée")));
				extended = extension[GNU_EXTENSION_IS_EXTENDED] != 0;
			}
		} else if (sparseMajor == 1) {
			// pax GNU.sparse 1.0 : carte décimale en tête des données, alignée sur
			// 512.
			record.sparse = true;
			const uint64_t mapStart = reader.Tell();
			uint64_t consumedMap = 0;
			auto nextNumber = [&]() -> Option<uint64_t> {
				uint64_t value = 0;
				int digits = 0;
				for (;;) {
					uint8_t c = reader.U8();
					++consumedMap;
					if (!reader.Ok() || consumedMap > size)
						return NONE;
					if (c == '\n')
						return digits > 0 ? Some(value) : Option<uint64_t>(NONE);
					if (c < '0' || c > '9' || digits >= 19)
						return NONE;
					value = value * 10 + uint64_t(c - '0');
					++digits;
				}
			};
			Option<uint64_t> count = nextNumber();
			if (count.IsNone() || count.Unwrap() > MAX_SPARSE_SEGMENTS)
				return Some(MakeError(ErrorKind::CORRUPT,
									  String("tar : carte pax de fichier creux invalide")));
			for (uint64_t k = 0; k < count.Unwrap(); ++k) {
				Option<uint64_t> at = nextNumber();
				Option<uint64_t> length = nextNumber();
				if (at.IsNone() || length.IsNone())
					return Some(MakeError(ErrorKind::CORRUPT,
										  String("tar : carte pax de fichier creux invalide")));
				record.segments.push_back({at.Unwrap(), length.Unwrap()});
			}
			const uint64_t mapBlocks = ((consumedMap + BLOCK - 1) / BLOCK) * BLOCK;
			if (mapBlocks > size)
				return Some(MakeError(ErrorKind::CORRUPT,
									  String("tar : carte pax de fichier creux tronquée")));
			(void)reader.Seek(mapStart + mapBlocks);
			size -= mapBlocks;
			realSize = sparseRealSize.UnwrapOr(size);
		} else if (!sparseMap.IsEmpty()) {
			// pax GNU.sparse 0.1 : « offset,taille,offset,taille… ».
			record.sparse = true;
			std::vector<String> numbers = sparseMap.Split(',');
			if (numbers.size() % 2 != 0 || numbers.size() / 2 > MAX_SPARSE_SEGMENTS)
				return Some(
					MakeError(ErrorKind::CORRUPT, String("tar : GNU.sparse.map invalide")));
			for (size_t k = 0; k < numbers.size(); k += 2) {
				Option<uint64_t> at = ParseDecimal(numbers[k]);
				Option<uint64_t> length = ParseDecimal(numbers[k + 1]);
				if (at.IsNone() || length.IsNone())
					return Some(
						MakeError(ErrorKind::CORRUPT, String("tar : GNU.sparse.map invalide")));
				record.segments.push_back({at.Unwrap(), length.Unwrap()});
			}
			realSize = sparseRealSize.UnwrapOr(size);
		}

		const uint64_t dataOffset = reader.Tell();
		if (hasData && size > total - dataOffset)
			return Some(
				MakeError(ErrorKind::CORRUPT,
						  String::Format("tar : données de « %s » tronquées", name.CStr())));
		if (hasData && !reader.Seek(dataOffset + ((size + BLOCK - 1) / BLOCK) * BLOCK)) {
			// Le dernier bloc peut manquer de bourrage : toléré si les
			// données elles-mêmes sont complètes.
			const bool failed = m_decoded.IsSome() && m_decoded.Value().state->error.IsSome();
			if (failed || (knownTotal < 0 && stream.Tell() < Sint64(dataOffset + size)))
				return Some(StreamFailure(
					String::Format("tar : données de « %s » tronquées", name.CStr()).CStr()));
			truncatedTail = true;
		}

		record.dataOffset = dataOffset;
		record.storedSize = hasData ? size : 0;
		switch (type) {
		case '0':
		case '\0':
		case '7':
		case 'S':
			entry.type = EntryType::FILE;
			break;
		case '1':
			entry.type = EntryType::FILE;
			record.hardLink = true;
			break;
		case '2':
			entry.type = EntryType::SYMLINK;
			break;
		case '5':
		case 'D':
			entry.type = EntryType::DIRECTORY;
			break;
		default:
			continue; // périphériques, FIFO, étiquette de volume… ignorés
		}
		// Vieux tar : dossier signalé par un « / » final sur un type fichier.
		if (entry.type == EntryType::FILE && name.EndsWith("/") && !record.hardLink)
			entry.type = EntryType::DIRECTORY;
		auto normalized = NormalizePath(name);
		entry.path = normalized.IsOk() ? normalized.Value() : name;
		if (entry.path.IsEmpty())
			continue; // « ./ » : la racine elle-même
		entry.size = entry.type == EntryType::FILE && !record.hardLink ? realSize : 0;
		entry.packedSize = m_format == Format::TAR ? entry.size : 0;
		// Dans un tar compressé, chaque entrée l'est par le flux qui l'enveloppe.
		const char* container = ContainerMethod(m_format);
		entry.method =
			String(record.hardLink ? "lien physique" : (record.sparse ? "creux" : container));
		if (entry.type == EntryType::SYMLINK) {
			entry.linkTarget = link;
			entry.size = link.GetSize();
		} else if (record.hardLink) {
			auto target = NormalizePath(link);
			entry.linkTarget = target.IsOk() ? target.Value() : link;
			for (size_t i = m_entries.size(); i-- > 0;) {
				if (m_entries[i].path == entry.linkTarget) {
					entry.size = m_entries[i].size;
					break;
				}
			}
		}
		m_entries.push_back(std::move(entry));
		m_records.push_back(record);
	}
	if (pendingExtension)
		return Some(
			MakeError(ErrorKind::CORRUPT, String("tar : en-tête d'extension sans entrée")));
	return NONE;
}

// ── TarWriter ────────────────────────────────────────────────────────────────

Result<bool, ArchiveError> TarWriter::Write(const VirtualFs& fs, sdl3::IOStream& out, const WriteOptions& options) {
	if (options.encryption != Encryption::NONE)
		return Err(MakeError(ErrorKind::UNSUPPORTED,
							 String("tar : le format ne gère pas le chiffrement")));
	if (m_format == Format::TAR) {
		// Positions comptées : la sortie peut être séquentielle.
		auto counted = OpenCountingSink(out, std::make_shared<uint64_t>(0));
		if (counted.IsError())
			return Err(counted.Error());
		return WriteTar(fs, counted.Value().io, options);
	}
	auto sink = OpenBorrowedStream(out);
	if (sink.IsError())
		return Err(sink.Error());
	auto encoder = OpenCompressor(std::move(sink).Unwrap(), CompressionOf(m_format), options);
	if (encoder.IsError())
		return Err(encoder.Error());
	auto written = WriteTar(fs, encoder.Value().io, options);
	if (written.IsError()) {
		(void)encoder.Value().io.Close();
		// L'erreur du compresseur est plus parlante qu'un échec d'écriture.
		if (encoder.Value().state->error.IsSome())
			return Err(encoder.Value().state->error.Value());
		return written;
	}
	return FinishStream(encoder.Value());
}

Result<bool, ArchiveError> TarWriter::WriteTar(const VirtualFs& fs, sdl3::IOStream& out, const WriteOptions& options) {
	using namespace detail::tar;
	const int64_t now = options.defaultTime != 0 ? options.defaultTime : CurrentUnixTime();
	BinaryWriter writer(out);
	const uint64_t start = writer.Tell();
	for (VirtualFs::NodeId id : fs.Flatten()) {
		const VirtualFs::Node& node = fs.Get(id);
		const EntryInfo& info = node.info;
		// Contenu en flux ; taille inconnue (0 annoncé, ex. un .bz2 dont la
		// taille n'est connue qu'en le décompressant) : lu en mémoire.
		Option<ArchiveStream> content = NONE;
		Bytes buffered;
		uint64_t size = 0;
		if (node.IsFile()) {
			auto opened = fs.OpenContent(id);
			if (opened.IsError())
				return Err(opened.Error());
			if (info.size == 0) {
				auto loaded = ReadStreamToEnd(opened.Value(), UINT64_MAX);
				if (loaded.IsError())
					return Err(loaded.Error());
				buffered = std::move(loaded).Unwrap();
				size = buffered.size();
			} else {
				content = Some(std::move(opened).Unwrap());
				size = info.size;
			}
		}
		const String path = node.IsDirectory() ? info.path + "/" : info.path;
		const int64_t modified = info.modifiedTime != 0 ? info.modifiedTime : now;
		const char type =
			node.IsDirectory() ? '5' : (info.type == EntryType::SYMLINK ? '2' : '0');
		const uint32_t mode = info.mode != 0 ? info.mode : (node.IsDirectory() ? 0755u : 0644u);

		// Répartition ustar prefix/name, sinon pax.
		String name = path, prefix;
		bool needPath = false;
		if (path.GetSize() > NAME_SIZE) {
			needPath = true;
			const size_t limit = std::min(path.GetSize() - 1, PREFIX_SIZE);
			for (size_t cut = limit + 1; cut-- > 0;) {
				if (path.CharAt(cut) == '/' && path.GetSize() - cut - 1 <= NAME_SIZE &&
					path.GetSize() - cut - 1 > 0) {
					prefix = path.Substr(0, cut);
					name = path.Substr(cut + 1);
					needPath = false;
					break;
				}
			}
		}
		const bool needLink = info.linkTarget.GetSize() > LINK_SIZE;
		const bool needSize = size > 077777777777ull;
		const bool needTime = modified < 0 || modified > 077777777777ll;
		if (needPath || needLink || needSize || needTime) {
			String pax;
			auto record = [&pax](const char* key, const String& value) {
				// La longueur inclut ses propres chiffres : point fixe.
				const size_t body = 1 + std::strlen(key) + 1 + value.GetSize() + 1;
				size_t length = body + 1;
				while (String::Format("%zu", length).GetSize() + body != length)
					length = String::Format("%zu", length).GetSize() + body;
				pax.Concat(String::Format("%zu %s=", length, key));
				pax.Concat(value);
				pax.Concat("\n");
			};
			if (needPath)
				record("path", path);
			if (needLink)
				record("linkpath", info.linkTarget);
			if (needSize)
				record("size", String::Format("%llu", static_cast<unsigned long long>(size)));
			if (needTime)
				record("mtime", String::Format("%lld", static_cast<long long>(modified)));
			String paxName = "PaxHeaders/" + BaseName(info.path);
			if (paxName.GetSize() > NAME_SIZE)
				paxName = paxName.Substr(0, NAME_SIZE);
			WriteHeader(writer, paxName, String(), 'x', 0644, pax.GetSize(),
						modified < 0 ? 0 : std::min<int64_t>(modified, 077777777777ll),
						String());
			writer.WriteString(pax);
			writer.Fill((BLOCK - pax.GetSize() % BLOCK) % BLOCK);
			if (needPath) {
				name = path.Substr(0, NAME_SIZE);
				prefix.Clear();
			}
		}
		WriteHeader(writer, name, prefix, type, mode, size,
					modified < 0 ? 0 : std::min<int64_t>(modified, 077777777777ll),
					needLink ? info.linkTarget.Substr(0, LINK_SIZE) : info.linkTarget);
		if (content.IsSome()) {
			auto copied = CopyStreamExactly(content.Value(), out, size, info.path);
			if (copied.IsError())
				return copied;
		} else {
			writer.Write(buffered);
		}
		writer.Fill((BLOCK - size % BLOCK) % BLOCK);
		if (!writer.Ok())
			return Err(MakeError(ErrorKind::IO, String("tar : écriture impossible")));
	}
	writer.Fill(2 * BLOCK);
	const uint64_t length = writer.Tell() - start;
	writer.Fill((RECORD - length % RECORD) % RECORD);
	if (!writer.Ok())
		return Err(MakeError(ErrorKind::IO, String("tar : écriture impossible")));
	return Ok(true);
}

void TarWriter::WriteHeader(BinaryWriter& writer, const String& name, const String& prefix,
		char type, uint32_t mode, uint64_t size, int64_t modified,
		const String& link) {
	using namespace detail::tar;
	Header header{};
	// Copies bornées par la taille de la VUE (cf. String::View) : GCC -O3
	// sait alors qu'une chaîne vide ne fournit aucun octet.
	auto put = [&header](size_t offset, size_t width, const String& text) {
		const StringView view = text.View();
		std::memcpy(header.data() + offset, view.GetData(), std::min(width, view.GetSize()));
	};
	auto octal = [&header](size_t offset, size_t width, uint64_t value) {
		// width - 1 chiffres octaux + NUL ; au-delà : base 256 (GNU).
		const String digits =
			String::Format("%0*llo", int(width - 1), static_cast<unsigned long long>(value));
		if (const StringView view = digits.View(); view.GetSize() <= width - 1) {
			std::memcpy(header.data() + offset, view.GetData(), view.GetSize());
			return;
		}
		for (size_t i = width; i-- > 1;) {
			header[offset + i] = uint8_t(value);
			value >>= 8;
		}
		header[offset] = 0x80;
	};
	put(NAME, NAME_SIZE, name);
	octal(MODE, ID_SIZE, mode & 07777);
	octal(UID, ID_SIZE, 0);
	octal(GID, ID_SIZE, 0);
	octal(SIZE, NUMBER12_SIZE, size);
	octal(MTIME, NUMBER12_SIZE, uint64_t(modified));
	header[TYPE] = uint8_t(type);
	put(LINK, LINK_SIZE, link);
	std::memcpy(header.data() + MAGIC,
				"ustar\0"
				"00",
				8);
	put(UNAME, OWNER_SIZE, String("root"));
	put(GNAME, OWNER_SIZE, String("root"));
	put(PREFIX, PREFIX_SIZE, prefix);
	std::memset(header.data() + CHECKSUM, ' ', CHECKSUM_SIZE);
	uint32_t sum = 0;
	for (uint8_t byte : header)
		sum += byte;
	const String checksum = String::Format("%06o", sum);
	// Copie bornée par la taille réelle (toujours 6 chiffres ici) : un
	// memcpy de 6 octets fixes fait croire à GCC -O2 qu'on lit au-delà.
	for (size_t i = 0; i < 6 && i < checksum.GetSize(); ++i)
		header[CHECKSUM + i] = uint8_t(checksum.CStr()[i]);
	header[CHECKSUM + 6] = 0;
	header[CHECKSUM + 7] = ' ';
	writer.Write(header);
}

} // namespace data::archive
