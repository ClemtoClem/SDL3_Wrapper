// Définitions de data/archive/archive_zip.hpp
#include "data/archive/archive_zip.hpp"

namespace data::archive {

namespace detail::zip {

const char* MethodName(uint16_t method) noexcept {
	switch (method) {
	case METHOD_STORE:
		return "stockée";
	case METHOD_DEFLATE:
		return "deflate";
	case METHOD_LZMA:
		return "lzma";
	case METHOD_XZ:
		return "xz (lzma2)";
	case METHOD_DEFLATE64:
		return "deflate64";
	case METHOD_BZIP2:
		return "bzip2";
	case METHOD_ZSTD:
		return "zstd";
	case METHOD_PPMD:
		return "ppmd";
	default:
		return "inconnue";
	}
}

size_t AesKeyLength(uint8_t strength) noexcept {
	return strength == 1 ? 16 : strength == 2 ? 24 : 32;
}

size_t AesSaltLength(uint8_t strength) noexcept {
	return strength == 1 ? 8 : strength == 2 ? 12 : 16;
}

// ── AesDecryptImpl ───────────────────────────────────────────────────────────

Result<size_t, ArchiveError> AesDecryptImpl::Produce(uint8_t* out, size_t max) {
	const uint64_t left = m_cipherLength - m_read;
	if (left == 0) {
		if (!m_verified) {
			uint8_t stored[10];
			auto got = StreamRead(m_inner, stored, sizeof(stored));
			if (got.IsError())
				return Err(got.Error());
			const auto mac = m_mac.Final();
			if (got.Value() != sizeof(stored) ||
				std::memcmp(mac.data(), stored, sizeof(stored)) != 0)
				return Err(MakeError(
					ErrorKind::CORRUPT,
					String::Format("%s : authentification AES échouée (données altérées)",
								   m_name.CStr())));
			m_verified = true;
		}
		return Ok(size_t(0));
	}
	auto got = StreamRead(m_inner, out, size_t(std::min<uint64_t>(max, left)));
	if (got.IsError())
		return got;
	if (got.Value() == 0)
		return Err(MakeError(ErrorKind::CORRUPT,
							 String::Format("%s : données AES tronquées", m_name.CStr())));
	const std::span<uint8_t> chunk(out, got.Value());
	m_mac.Update(chunk);
	m_ctr.Apply(chunk);
	m_read += chunk.size();
	return got;
}

bool AesDecryptImpl::Restart() {
	if (m_inner.io.Seek(0, SDL_IO_SEEK_SET) != 0)
		return false;
	m_ctr = AesCtrWinZip(m_aes);
	m_mac = m_hmac;
	m_read = 0;
	m_verified = false;
	return true;
}

// ── ZipCryptoDecryptImpl ─────────────────────────────────────────────────────

Result<size_t, ArchiveError> ZipCryptoDecryptImpl::Produce(uint8_t* out, size_t max) {
	auto got = StreamRead(m_inner, out, max);
	if (got.IsOk())
		m_keys.Decrypt(std::span<uint8_t>(out, got.Value()));
	return got;
}

bool ZipCryptoDecryptImpl::Restart() {
	if (m_inner.io.Seek(0, SDL_IO_SEEK_SET) != 0)
		return false;
	m_keys = m_initial;
	return true;
}

// ── AesEncryptImpl ───────────────────────────────────────────────────────────

Result<bool, ArchiveError> AesEncryptImpl::Consume(const uint8_t* data, size_t size) {
	if (auto header = EmitHeader(); header.IsError())
		return header;
	while (size > 0) {
		const size_t take = std::min(size, sizeof(m_scratch));
		std::memcpy(m_scratch, data, take);
		const std::span<uint8_t> chunk(m_scratch, take);
		m_ctr.Apply(chunk);
		m_mac.Update(chunk);
		if (auto emitted = Emit(m_scratch, take); emitted.IsError())
			return emitted;
		data += take;
		size -= take;
	}
	return Ok(true);
}

Result<bool, ArchiveError> AesEncryptImpl::Finish() {
	if (auto header = EmitHeader(); header.IsError())
		return header;
	const auto mac = m_mac.Final();
	return Emit(mac.data(), 10);
}

Result<bool, ArchiveError> AesEncryptImpl::EmitHeader() {
	if (m_header.empty())
		return Ok(true);
	auto emitted = Emit(m_header.data(), m_header.size());
	m_header.clear();
	return emitted;
}

// ── ZipCryptoEncryptImpl ─────────────────────────────────────────────────────

Result<bool, ArchiveError> ZipCryptoEncryptImpl::Consume(const uint8_t* data, size_t size) {
	if (auto header = EmitHeader(); header.IsError())
		return header;
	while (size > 0) {
		const size_t take = std::min(size, sizeof(m_scratch));
		std::memcpy(m_scratch, data, take);
		m_keys.Encrypt(std::span<uint8_t>(m_scratch, take));
		if (auto emitted = Emit(m_scratch, take); emitted.IsError())
			return emitted;
		data += take;
		size -= take;
	}
	return Ok(true);
}

Result<bool, ArchiveError> ZipCryptoEncryptImpl::EmitHeader() {
	if (m_header.empty())
		return Ok(true);
	m_keys.Encrypt(m_header);
	auto emitted = Emit(m_header.data(), m_header.size());
	m_header.clear();
	return emitted;
}

std::span<const uint8_t> PasswordBytes(const String& password) noexcept {
	return {reinterpret_cast<const uint8_t*>(password.CStr()), password.GetSize()};
}

} // namespace detail::zip

// ── ZipReader ────────────────────────────────────────────────────────────────

Result<std::unique_ptr<ZipReader>, ArchiveError> ZipReader::Open(ArchiveSource source, const ReadOptions& options) {
	std::unique_ptr<ZipReader> reader(new ZipReader(std::move(source), options));
	if (auto error = reader->Index(); error.IsSome())
		return Err(error.Unwrap());
	return Ok(std::move(reader));
}

const std::vector<EntryInfo>& ZipReader::Entries() const noexcept {
	return m_entries;
}

void ZipReader::SetPassword(const String& password) {
	m_options.password = password;
	ResolveLinkTargets();
}

Result<ArchiveStream, ArchiveError> ZipReader::OpenEntry(size_t index) {
	using namespace detail::zip;
	if (index >= m_entries.size())
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT, String("entrée inexistante")));
	const EntryInfo& entry = m_entries[index];
	const Record& record = m_records[index];
	if (entry.type == EntryType::DIRECTORY)
		return OpenMemoryStream(Bytes());
	if (record.flags & FLAG_STRONG_ENCRYPTION)
		return Err(MakeError(
			ErrorKind::UNSUPPORTED,
			String::Format("%s : chiffrement fort PKWARE non géré", entry.path.CStr())));
	auto offset = DataOffset(entry, record);
	if (offset.IsError())
		return Err(offset.Error());
	const uint64_t dataOffset = offset.Value();

	Result<ArchiveStream, ArchiveError> packed = Err(ArchiveError{});
	ErrorKind mismatch = ErrorKind::CORRUPT;
	if (record.flags & FLAG_ENCRYPTED) {
		packed = record.aes ? OpenAes(entry, record, dataOffset)
							: OpenTraditional(entry, record, dataOffset);
		// Le vérificateur traditionnel ne tient que sur un octet : un CRC
		// faux signale presque toujours un mauvais mot de passe.
		if (!record.aes)
			mismatch = ErrorKind::WRONG_PASSWORD;
	} else {
		packed = OpenSubStream(m_source.Stream(), dataOffset, record.compressedSize);
	}
	if (packed.IsError())
		return packed;
	// AES : le code d'authentification suit les données ; le décodeur
	// lit une vue du flux déchiffré, qui est ensuite lu jusqu'au bout.
	std::shared_ptr<ArchiveStream> authenticated;
	if (record.aes) {
		const uint64_t cipherLength =
			record.compressedSize - AesSaltLength(record.aesStrength) - 2 - 10;
		authenticated = std::make_shared<ArchiveStream>(std::move(packed).Unwrap());
		packed = OpenSharedSubStream(authenticated, 0, cipherLength);
		if (packed.IsError())
			return packed;
	}
	auto decoded = OpenDecoder(entry, record, std::move(packed).Unwrap());
	if (decoded.IsError())
		return decoded;
	if (authenticated) {
		decoded = OpenDrainingStream(std::move(decoded).Unwrap(), authenticated);
		if (decoded.IsError())
			return decoded;
	}
	// AE-2 met le CRC à zéro : l'authentification HMAC le remplace.
	const bool checkCrc = m_options.verifyChecksums && !(record.aes && record.aesVersion == 2);
	return OpenCheckedStream(std::move(decoded).Unwrap(), Some(record.uncompressedSize),
							 checkCrc ? Option<uint32_t>(Some(record.crc))
									  : Option<uint32_t>(NONE),
							 entry.path, mismatch);
}

Result<uint64_t, ArchiveError> ZipReader::DataOffset(const EntryInfo& entry, const detail::zip::Record& record) {
	BinaryReader reader(m_source.Stream());
	(void)reader.Seek(record.localOffset + m_offsetShift);
	const uint32_t signature = reader.U32Le();
	(void)reader.Skip(22);
	const uint16_t nameLength = reader.U16Le();
	const uint16_t extraLength = reader.U16Le();
	if (!reader.Ok() || signature != detail::zip::LOCAL_SIGNATURE)
		return Err(MakeError(ErrorKind::CORRUPT,
							 String::Format("%s : en-tête local invalide", entry.path.CStr())));
	const uint64_t dataOffset =
		record.localOffset + m_offsetShift + 30 + nameLength + extraLength;
	if (dataOffset > m_source.Size() || record.compressedSize > m_source.Size() - dataOffset)
		return Err(MakeError(
			ErrorKind::CORRUPT,
			String::Format("%s : données hors de l'archive (tronquée ?)", entry.path.CStr())));
	return Ok(dataOffset);
}

Result<ArchiveStream, ArchiveError> ZipReader::OpenDecoder(const EntryInfo& entry, const detail::zip::Record& record, ArchiveStream packed) {
	using namespace detail::zip;
	const uint64_t size = record.uncompressedSize;
	switch (record.method) {
	case METHOD_STORE:
		return Ok(std::move(packed));
	case METHOD_DEFLATE:
	case METHOD_DEFLATE64:
		return OpenInflateStream(std::move(packed), record.method == METHOD_DEFLATE64,
								 Some(size));
	case METHOD_BZIP2:
		return OpenBzip2Stream(std::move(packed), Some(size));
	case METHOD_LZMA: {
		// Version du SDK (2 octets), taille des propriétés, propriétés.
		uint8_t header[4 + 5];
		auto got = StreamRead(packed, header, 4);
		if (got.IsError())
			return Err(got.Error());
		const uint16_t propertiesSize = uint16_t(header[2] | (header[3] << 8));
		if (got.Value() != 4 || propertiesSize != 5)
			return Err(
				MakeError(ErrorKind::CORRUPT,
						  String::Format("%s : en-tête LZMA invalide", entry.path.CStr())));
		auto properties = StreamRead(packed, header + 4, 5);
		if (properties.IsError())
			return Err(properties.Error());
		auto props = LzmaProperties::Decode(std::span<const uint8_t>(header + 4, 5));
		if (properties.Value() != 5 || props.IsError())
			return Err(
				MakeError(ErrorKind::CORRUPT,
						  String::Format("%s : propriétés LZMA invalides", entry.path.CStr())));
		return OpenLzmaStream(std::move(packed), props.Value(), size,
							  (record.flags & FLAG_LZMA_END_MARKER) != 0);
	}
	case METHOD_ZSTD:
		return OpenZstdStream(std::move(packed), Some(size));
	case METHOD_XZ:
		return OpenXzStream(std::move(packed));
	case METHOD_PPMD:
		return OpenPpmd8ZipStream(std::move(packed), size);
	default:
		return Err(
			MakeError(ErrorKind::UNSUPPORTED,
					  String::Format("%s : méthode %s (%u) non gérée", entry.path.CStr(),
									 MethodName(record.method), unsigned(record.method))));
	}
}

Result<ArchiveStream, ArchiveError> ZipReader::OpenAes(const EntryInfo& entry, const detail::zip::Record& record, uint64_t dataOffset) {
	using namespace detail::zip;
	if (m_options.password.IsEmpty())
		return Err(MakeError(ErrorKind::PASSWORD_REQUIRED,
							 String::Format("%s est chiffré (AES)", entry.path.CStr())));
	const size_t keyLength = AesKeyLength(record.aesStrength);
	const size_t saltLength = AesSaltLength(record.aesStrength);
	if (record.compressedSize < saltLength + 2 + 10)
		return Err(MakeError(ErrorKind::CORRUPT,
							 String::Format("%s : données AES tronquées", entry.path.CStr())));
	auto header = ReadRange(m_source.Stream(), dataOffset, saltLength + 2);
	if (header.IsError())
		return Err(MakeError(ErrorKind::CORRUPT, String::Format("%s : %s", entry.path.CStr(),
																header.Error().CStr())));
	const Bytes& h = header.Value();
	const Bytes keys = Pbkdf2HmacSha1(PasswordBytes(m_options.password),
									  std::span<const uint8_t>(h).subspan(0, saltLength), 1000,
									  2 * keyLength + 2);
	if (keys[2 * keyLength] != h[saltLength] || keys[2 * keyLength + 1] != h[saltLength + 1])
		return Err(MakeError(ErrorKind::WRONG_PASSWORD,
							 String::Format("%s : mot de passe incorrect", entry.path.CStr())));
	auto aes = Aes::Create(std::span<const uint8_t>(keys).subspan(0, keyLength));
	if (aes.IsError())
		return Err(MakeError(ErrorKind::CORRUPT, aes.Error()));
	const Hmac<Sha1> hmac(std::span<const uint8_t>(keys).subspan(keyLength, keyLength));
	const uint64_t length = record.compressedSize - saltLength - 2;
	auto window = OpenSubStream(m_source.Stream(), dataOffset + saltLength + 2, length);
	if (window.IsError())
		return window;
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<AesDecryptImpl>(std::move(window).Unwrap(), length - 10,
													   aes.Value(), hmac, entry.path, state),
					  state);
}

Result<ArchiveStream, ArchiveError> ZipReader::OpenTraditional(const EntryInfo& entry, const detail::zip::Record& record,
		uint64_t dataOffset) {
	using namespace detail::zip;
	if (m_options.password.IsEmpty())
		return Err(MakeError(ErrorKind::PASSWORD_REQUIRED,
							 String::Format("%s est chiffré", entry.path.CStr())));
	if (record.compressedSize < 12)
		return Err(
			MakeError(ErrorKind::CORRUPT, String::Format("%s : en-tête de chiffrement tronqué",
														 entry.path.CStr())));
	auto header = ReadRange(m_source.Stream(), dataOffset, 12);
	if (header.IsError())
		return Err(MakeError(ErrorKind::CORRUPT, String::Format("%s : %s", entry.path.CStr(),
																header.Error().CStr())));
	Bytes h = std::move(header).Unwrap();
	ZipCrypto keys(m_options.password);
	keys.Decrypt(h);
	const uint8_t check = (record.flags & FLAG_DATA_DESCRIPTOR) ? uint8_t(record.dosTime >> 8)
																: uint8_t(record.crc >> 24);
	if (h[11] != check)
		return Err(MakeError(ErrorKind::WRONG_PASSWORD,
							 String::Format("%s : mot de passe incorrect", entry.path.CStr())));
	auto window = OpenSubStream(m_source.Stream(), dataOffset + 12, record.compressedSize - 12);
	if (window.IsError())
		return window;
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<ZipCryptoDecryptImpl>(
						  std::move(window).Unwrap(), record.compressedSize - 12, keys, state),
					  state);
}

Option<ArchiveError> ZipReader::Index() {
	using namespace detail::zip;
	sdl3::IOStream& stream = m_source.Stream();
	const uint64_t size = m_source.Size();
	if (size < 22)
		return Some(MakeError(ErrorKind::FORMAT, String("zip : fichier trop court")));

	// Fin du répertoire central : cherchée dans les 22 + 65 535 derniers
	// octets.
	const uint64_t tailSize = std::min<uint64_t>(size, 22 + 65535);
	auto tail = ReadRange(stream, size - tailSize, tailSize);
	if (tail.IsError())
		return Some(MakeError(ErrorKind::IO, tail.Error()));
	const Bytes& t = tail.Value();
	Option<uint64_t> endPosition = NONE;
	for (size_t at = t.size() - 22 + 1; at-- > 0;) {
		if (t[at] == 0x50 && t[at + 1] == 0x4B && t[at + 2] == 0x05 && t[at + 3] == 0x06) {
			endPosition = Some(size - tailSize + at);
			break;
		}
	}
	if (endPosition.IsNone())
		return Some(MakeError(ErrorKind::FORMAT,
							  String("zip : fin du répertoire central introuvable")));

	BinaryReader reader(stream);
	(void)reader.Seek(endPosition.Unwrap());
	(void)reader.U32Le();
	const uint16_t diskNumber = reader.U16Le();
	const uint16_t directoryDisk = reader.U16Le();
	(void)reader.U16Le();
	uint64_t entryCount = reader.U16Le();
	uint64_t directorySize = reader.U32Le();
	uint64_t directoryOffset = reader.U32Le();
	const uint16_t commentLength = reader.U16Le();
	m_comment = reader.ReadString(std::min<uint64_t>(commentLength, reader.Remaining()));
	if (!reader.Ok())
		return Some(
			MakeError(ErrorKind::CORRUPT, String("zip : fin du répertoire central tronquée")));
	if (diskNumber != 0 || directoryDisk != 0)
		return Some(MakeError(ErrorKind::UNSUPPORTED,
							  String("zip : archives multi-volumes non gérées")));

	// ZIP64 : cherché même si les champs 32 bits ne sont pas saturés (Info-ZIP
	// l'écrit pour les entrées lues en flux) ; sa position sert aussi à
	// calculer le décalage des données en tête.
	uint64_t directoryEnd = endPosition.Unwrap();
	const bool saturated =
		entryCount == 0xFFFF || directorySize == 0xFFFFFFFFu || directoryOffset == 0xFFFFFFFFu;
	bool locatorFound = false;
	if (endPosition.Unwrap() >= 20) {
		(void)reader.Seek(endPosition.Unwrap() - 20);
		locatorFound = reader.U32Le() == END64_LOCATOR_SIGNATURE && reader.Ok();
	}
	if (locatorFound) {
		(void)reader.U32Le();
		const uint64_t end64Offset = reader.U64Le();
		(void)reader.U32Le();
		// L'enregistrement précède normalement le localisateur ; sinon (données
		// d'extension) on se fie à l'offset annoncé.
		Option<uint64_t> end64Position = NONE;
		if (endPosition.Unwrap() >= 20 + 56) {
			(void)reader.Seek(endPosition.Unwrap() - 20 - 56);
			if (reader.U32Le() == END64_SIGNATURE && reader.Ok())
				end64Position = Some(endPosition.Unwrap() - 20 - 56);
		}
		if (end64Position.IsNone() && end64Offset < endPosition.Unwrap()) {
			(void)reader.Seek(end64Offset);
			if (reader.U32Le() == END64_SIGNATURE && reader.Ok())
				end64Position = Some(end64Offset);
		}
		if (end64Position.IsNone()) {
			if (saturated)
				return Some(MakeError(ErrorKind::CORRUPT,
									  String("zip : enregistrement ZIP64 introuvable")));
		} else {
			(void)reader.Seek(end64Position.Unwrap() + 4);
			(void)reader.U64Le(); // taille de l'enregistrement
			(void)reader.U16Le(); // version créatrice
			(void)reader.U16Le(); // version requise
			const uint32_t disk64 = reader.U32Le();
			const uint32_t directoryDisk64 = reader.U32Le();
			(void)reader.U64Le(); // entrées sur ce disque
			entryCount = reader.U64Le();
			directorySize = reader.U64Le();
			directoryOffset = reader.U64Le();
			directoryEnd = end64Position.Unwrap();
			if (!reader.Ok())
				return Some(MakeError(ErrorKind::CORRUPT,
									  String("zip : enregistrement ZIP64 tronqué")));
			if (disk64 != 0 || directoryDisk64 != 0)
				return Some(MakeError(ErrorKind::UNSUPPORTED,
									  String("zip : archives multi-volumes non gérées")));
		}
	} else if (saturated && entryCount != 0xFFFF) {
		return Some(MakeError(ErrorKind::CORRUPT, String("zip : localisateur ZIP64 absent")));
	}
	if (directorySize > directoryEnd)
		return Some(MakeError(ErrorKind::CORRUPT,
							  String("zip : taille du répertoire central incohérente")));
	// Données en tête (auto-extractible) : le répertoire se termine juste
	// avant la fin ; l'écart avec l'offset annoncé décale tout le fichier.
	const uint64_t actualDirectory = directoryEnd - directorySize;
	m_offsetShift = actualDirectory >= directoryOffset ? actualDirectory - directoryOffset : 0;

	auto directoryBytes = ReadRange(stream, actualDirectory, directorySize);
	if (directoryBytes.IsError())
		return Some(MakeError(ErrorKind::CORRUPT, directoryBytes.Error()));
	auto memory = MemoryStream::FromBytes(std::move(directoryBytes).Unwrap());
	if (memory.IsError())
		return Some(MakeError(ErrorKind::IO, memory.Error()));
	BinaryReader central(memory.Value().Stream());
	for (uint64_t i = 0; i < entryCount; ++i) {
		if (central.U32Le() != CENTRAL_SIGNATURE)
			return Some(MakeError(ErrorKind::CORRUPT,
								  String("zip : entrée du répertoire central invalide")));
		Record record;
		record.versionMadeBy = central.U16Le();
		(void)central.U16Le();
		record.flags = central.U16Le();
		record.method = central.U16Le();
		record.dosTime = central.U16Le();
		record.dosDate = central.U16Le();
		record.crc = central.U32Le();
		record.compressedSize = central.U32Le();
		record.uncompressedSize = central.U32Le();
		const uint16_t nameLength = central.U16Le();
		const uint16_t extraLength = central.U16Le();
		const uint16_t entryCommentLength = central.U16Le();
		(void)central.U16Le(); // disque
		(void)central.U16Le(); // attributs internes
		record.externalAttributes = central.U32Le();
		record.localOffset = central.U32Le();
		Bytes rawName = central.ReadBytes(nameLength);
		Bytes extra = central.ReadBytes(extraLength);
		(void)central.Skip(entryCommentLength);
		if (!central.Ok())
			return Some(
				MakeError(ErrorKind::CORRUPT, String("zip : répertoire central tronqué")));

		EntryInfo entry;
		String name =
			(record.flags & FLAG_UTF8)
				? String(reinterpret_cast<const char*>(rawName.data()), rawName.size())
				: Cp437ToUtf8(rawName);
		entry.modifiedTime = UnixFromDos(record.dosDate, record.dosTime);
		if (auto error = ParseExtra(extra, record, entry); error.IsSome())
			return error;

		const uint8_t host = uint8_t(record.versionMadeBy >> 8);
		const uint32_t unixMode =
			(host == 3 || host == 19) ? (record.externalAttributes >> 16) : 0;
		const bool directory = name.EndsWith("/") || name.EndsWith("\\") ||
							   (record.externalAttributes & 0x10) != 0 ||
							   (unixMode & UNIX_TYPE_MASK) == UNIX_DIRECTORY;
		auto normalized = NormalizePath(name);
		entry.path = normalized.IsOk()
						 ? normalized.Value()
						 : name; // chemin dangereux conservé : refusé à l'extraction
		entry.type = directory
						 ? EntryType::DIRECTORY
						 : ((unixMode & UNIX_TYPE_MASK) == UNIX_SYMLINK ? EntryType::SYMLINK
																		: EntryType::FILE);
		entry.mode = unixMode & 07777;
		entry.size = record.uncompressedSize;
		entry.packedSize = record.compressedSize;
		entry.crc32 = (record.aes && record.aesVersion == 2)
						  ? Option<uint32_t>(NONE)
						  : Option<uint32_t>(Some(record.crc));
		entry.encrypted = (record.flags & FLAG_ENCRYPTED) != 0;
		entry.method = String(MethodName(record.method));
		if (entry.encrypted)
			entry.method.Concat(
				record.aes ? String::Format(" + aes-%zu", AesKeyLength(record.aesStrength) * 8)
						   : String(" + zipcrypto"));
		m_entries.push_back(std::move(entry));
		m_records.push_back(record);
	}
	ResolveLinkTargets();
	return NONE;
}

void ZipReader::ResolveLinkTargets() {
	for (size_t i = 0; i < m_entries.size(); ++i) {
		if (m_entries[i].type != EntryType::SYMLINK || m_entries[i].size > 4096 ||
			!m_entries[i].linkTarget.IsEmpty() ||
			(m_entries[i].encrypted && m_options.password.IsEmpty()))
			continue;
		auto target = Extract(i);
		if (target.IsOk())
			m_entries[i].linkTarget = String(
				reinterpret_cast<const char*>(target.Value().data()), target.Value().size());
	}
}

Option<ArchiveError> ZipReader::ParseExtra(const Bytes& extra, detail::zip::Record& record, EntryInfo& entry) {
	using namespace detail::zip;
	auto memory = ViewStream(extra);
	if (memory.IsError())
		return Some(MakeError(ErrorKind::IO, String("flux mémoire impossible")));
	BinaryReader reader(memory.Value());
	while (reader.Remaining() >= 4) {
		const uint16_t id = reader.U16Le();
		const uint16_t length = reader.U16Le();
		const uint64_t end = reader.Tell() + length;
		if (end > reader.Size())
			break; // extra mal formé : ignoré (comme Info-ZIP)
		if (id == 0x0001) {
			// ZIP64 : uniquement les champs saturés, dans cet ordre.
			if (record.uncompressedSize == 0xFFFFFFFFu && reader.Tell() + 8 <= end)
				record.uncompressedSize = reader.U64Le();
			if (record.compressedSize == 0xFFFFFFFFu && reader.Tell() + 8 <= end)
				record.compressedSize = reader.U64Le();
			if (record.localOffset == 0xFFFFFFFFu && reader.Tell() + 8 <= end)
				record.localOffset = reader.U64Le();
		} else if (id == 0x9901 && length >= 7) {
			record.aes = true;
			record.aesVersion = reader.U16Le();
			const uint8_t vendorA = reader.U8(), vendorE = reader.U8();
			record.aesStrength = reader.U8();
			const uint16_t actualMethod = reader.U16Le();
			if (vendorA != 'A' || vendorE != 'E' || record.aesStrength < 1 ||
				record.aesStrength > 3)
				return Some(MakeError(ErrorKind::CORRUPT, String("zip : champ AES invalide")));
			if (record.method == METHOD_AES)
				record.method = actualMethod;
		} else if (id == 0x5455 && length >= 5) {
			const uint8_t flags = reader.U8();
			if (flags & 0x01)
				entry.modifiedTime = int64_t(reader.U32Le());
		}
		(void)reader.Seek(end);
	}
	if (record.method == METHOD_AES && !record.aes)
		return Some(
			MakeError(ErrorKind::CORRUPT, String("zip : méthode AES sans champ 0x9901")));
	return NONE;
}

// ── ZipWriter ────────────────────────────────────────────────────────────────

Result<bool, ArchiveError> ZipWriter::Write(const VirtualFs& fs, sdl3::IOStream& out, const WriteOptions& options) {
	using namespace detail::zip;
	if (options.encryption != Encryption::NONE && options.password.IsEmpty())
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT,
							 String("zip : chiffrement demandé sans mot de passe")));
	const int64_t now = options.defaultTime != 0 ? options.defaultTime : CurrentUnixTime();
	// Positions comptées (la sortie peut ne pas savoir se déplacer) ;
	// `start` sert à corriger les en-têtes quand elle le peut.
	const Sint64 start = out.Tell();
	const bool seekable = start >= 0 && out.GetSize() >= 0;
	auto counted = OpenCountingSink(out, std::make_shared<uint64_t>(0));
	if (counted.IsError())
		return Err(counted.Error());
	Output output{counted.Value().io, out, seekable ? uint64_t(start) : 0, seekable};
	BinaryWriter& writer = output.writer;

	std::vector<Central> centrals;
	for (VirtualFs::NodeId id : fs.Flatten()) {
		const VirtualFs::Node& node = fs.Get(id);
		Central central;
		central.name = node.IsDirectory() ? node.info.path + "/" : node.info.path;
		central.modifiedTime = node.info.modifiedTime != 0 ? node.info.modifiedTime : now;
		Record& record = central.record;
		record.versionMadeBy = uint16_t((3 << 8) | 63);
		DosFromUnix(central.modifiedTime, record.dosDate, record.dosTime);
		const uint32_t mode =
			node.info.mode != 0 ? node.info.mode : (node.IsDirectory() ? 0755u : 0644u);
		const uint32_t type =
			node.IsDirectory()
				? UNIX_DIRECTORY
				: (node.info.type == EntryType::SYMLINK ? UNIX_SYMLINK : UNIX_REGULAR);
		record.externalAttributes =
			((type | (mode & 07777)) << 16) | (node.IsDirectory() ? 0x10u : 0u);
		if (!IsAscii(central.name))
			record.flags |= FLAG_UTF8;
		record.localOffset = writer.Tell();
		if (node.IsDirectory()) {
			WriteLocalHeader(writer, central, false);
		} else {
			auto written = WriteEntry(fs, id, output, options, central);
			if (written.IsError())
				return written;
		}
		if (!writer.Ok())
			return Err(MakeError(ErrorKind::IO, String("zip : écriture impossible")));
		centrals.push_back(std::move(central));
	}

	const uint64_t directoryOffset = writer.Tell();
	for (const Central& central : centrals) {
		const Record& record = central.record;
		const bool sizes64 =
			record.uncompressedSize >= 0xFFFFFFFFu || record.compressedSize >= 0xFFFFFFFFu;
		const bool offset64 = record.localOffset >= 0xFFFFFFFFu;
		const uint16_t versionNeeded = sizes64 || offset64
										   ? std::max<uint16_t>(central.versionNeeded, 45)
										   : central.versionNeeded;
		Bytes extra = BuildExtra(record, central.modifiedTime, sizes64, offset64);
		writer.U32Le(CENTRAL_SIGNATURE);
		writer.U16Le(record.versionMadeBy);
		writer.U16Le(versionNeeded);
		writer.U16Le(record.flags);
		writer.U16Le(record.aes ? METHOD_AES : record.method);
		writer.U16Le(record.dosTime);
		writer.U16Le(record.dosDate);
		writer.U32Le(record.aes ? 0 : record.crc);
		writer.U32Le(sizes64 ? 0xFFFFFFFFu : uint32_t(record.compressedSize));
		writer.U32Le(sizes64 ? 0xFFFFFFFFu : uint32_t(record.uncompressedSize));
		writer.U16Le(uint16_t(central.name.GetSize()));
		writer.U16Le(uint16_t(extra.size()));
		writer.U16Le(0); // commentaire
		writer.U16Le(0); // disque
		writer.U16Le(0); // attributs internes
		writer.U32Le(record.externalAttributes);
		writer.U32Le(offset64 ? 0xFFFFFFFFu : uint32_t(record.localOffset));
		writer.WriteString(central.name);
		writer.Write(extra);
	}
	const uint64_t directoryEnd = writer.Tell();
	const uint64_t directorySize = directoryEnd - directoryOffset;
	const bool end64 = centrals.size() >= 0xFFFF || directoryOffset >= 0xFFFFFFFFu ||
					   directorySize >= 0xFFFFFFFFu;
	if (end64) {
		writer.U32Le(END64_SIGNATURE);
		writer.U64Le(44);
		writer.U16Le(uint16_t((3 << 8) | 63));
		writer.U16Le(45);
		writer.U32Le(0);
		writer.U32Le(0);
		writer.U64Le(centrals.size());
		writer.U64Le(centrals.size());
		writer.U64Le(directorySize);
		writer.U64Le(directoryOffset);
		writer.U32Le(END64_LOCATOR_SIGNATURE);
		writer.U32Le(0);
		writer.U64Le(directoryEnd);
		writer.U32Le(1);
	}
	writer.U32Le(END_SIGNATURE);
	writer.U16Le(0);
	writer.U16Le(0);
	writer.U16Le(end64 ? 0xFFFF : uint16_t(centrals.size()));
	writer.U16Le(end64 ? 0xFFFF : uint16_t(centrals.size()));
	writer.U32Le(end64 ? 0xFFFFFFFFu : uint32_t(directorySize));
	writer.U32Le(end64 ? 0xFFFFFFFFu : uint32_t(directoryOffset));
	writer.U16Le(0);
	if (!writer.Ok())
		return Err(MakeError(ErrorKind::IO, String("zip : écriture impossible")));
	return Ok(true);
}

Result<bool, ArchiveError> ZipWriter::WriteEntry(const VirtualFs& fs,
		VirtualFs::NodeId id, Output& output,
		const WriteOptions& options,
		Central& central) {
	using namespace detail::zip;
	BinaryWriter& writer = output.writer;
	Record& record = central.record;
	const EntryInfo& info = fs.Get(id).info;
	auto content = fs.OpenContent(id);
	if (content.IsError())
		return Err(content.Error());

	// Petit fichier : tout est connu avant d'écrire l'en-tête.
	if (info.type == EntryType::SYMLINK || info.size <= SMALL_ENTRY) {
		auto data = ReadStreamToEnd(content.Value(), SMALL_ENTRY, info.size);
		if (data.IsOk())
			return WriteSmallEntry(data.Value(), output, options, central);
		if (data.Error().kind != ErrorKind::LIMIT)
			return Err(data.Error());
		content = fs.OpenContent(id); // plus gros qu'annoncé : copie en flux
		if (content.IsError())
			return Err(content.Error());
	}

	const uint16_t method = MethodFor(options, info.size);
	SetMethod(method, central);
	// Sortie séquentielle ou octet de contrôle traditionnel inconnu (il
	// dépend du CRC) : tailles et CRC dans un descripteur après les données.
	const bool descriptor = !output.seekable || options.encryption == Encryption::ZIP_CRYPTO;
	if (descriptor)
		record.flags |= FLAG_DATA_DESCRIPTOR;
	MarkEncryption(options, central);
	// Champ ZIP64 réservé si le fichier approche 4 Gio (la compression
	// peut légèrement grossir les données).
	const bool zip64 = info.size >= 0xFF000000u;
	if (zip64)
		central.versionNeeded = std::max<uint16_t>(central.versionNeeded, 45);
	const uint64_t headerPosition = writer.Tell();
	WriteLocalHeader(writer, central, zip64);
	if (!writer.Ok())
		return Err(MakeError(ErrorKind::IO, String("zip : écriture impossible")));

	auto counter = std::make_shared<uint64_t>(0);
	auto sink = OpenCountingSink(output.stream, counter);
	if (sink.IsError())
		return Err(sink.Error());
	auto encrypted =
		OpenEncryption(std::move(sink).Unwrap(), options, uint8_t(record.dosTime >> 8));
	if (encrypted.IsError())
		return Err(encrypted.Error());
	auto encoder = OpenMethodEncoder(std::move(encrypted).Unwrap(), method, options, info.size);
	if (encoder.IsError())
		return Err(encoder.Error());
	std::vector<uint8_t> chunk(1 << 16);
	uint32_t crc = 0;
	uint64_t total = 0;
	for (;;) {
		auto done = StreamRead(content.Value(), chunk.data(), chunk.size());
		if (done.IsError())
			return Err(done.Error());
		if (done.Value() == 0)
			break;
		const std::span<const uint8_t> view(chunk.data(), done.Value());
		crc = Crc32(view, crc);
		total += view.size();
		if (auto written = StreamWrite(encoder.Value(), view); written.IsError())
			return written;
	}
	if (auto finished = FinishStream(encoder.Value()); finished.IsError())
		return finished;
	record.crc = crc;
	record.uncompressedSize = total;
	record.compressedSize = *counter;
	if (!zip64 && (total >= 0xFFFFFFFFu || *counter >= 0xFFFFFFFFu))
		return Err(
			MakeError(ErrorKind::LIMIT, String::Format("zip : %s dépasse 4 Gio alors que sa "
													   "taille annoncée est plus petite",
													   info.path.CStr())));

	const uint32_t storedCrc = record.aes ? 0 : crc;
	if (descriptor) {
		writer.U32Le(DESCRIPTOR_SIGNATURE);
		writer.U32Le(storedCrc);
		if (zip64) {
			writer.U64Le(record.compressedSize);
			writer.U64Le(record.uncompressedSize);
		} else {
			writer.U32Le(uint32_t(record.compressedSize));
			writer.U32Le(uint32_t(record.uncompressedSize));
		}
	} else {
		BinaryWriter patch(output.target);
		const uint64_t header = output.start + headerPosition;
		(void)patch.Seek(header + 14);
		patch.U32Le(storedCrc);
		if (zip64) {
			(void)patch.Seek(header + 30 + central.name.GetSize() + 4);
			patch.U64Le(record.uncompressedSize);
			patch.U64Le(record.compressedSize);
		} else {
			patch.U32Le(uint32_t(record.compressedSize));
			patch.U32Le(uint32_t(record.uncompressedSize));
		}
		(void)patch.Seek(output.start + writer.Tell());
		if (!patch.Ok())
			return Err(MakeError(ErrorKind::IO,
								 String("zip : correction de l'en-tête local impossible")));
	}
	if (!writer.Ok())
		return Err(MakeError(ErrorKind::IO, String("zip : écriture impossible")));
	return Ok(true);
}

Result<bool, ArchiveError> ZipWriter::WriteSmallEntry(const Bytes& data,
		Output& output,
		const WriteOptions& options,
		Central& central) {
	using namespace detail::zip;
	BinaryWriter& writer = output.writer;
	Record& record = central.record;
	record.crc = Crc32(data);
	record.uncompressedSize = data.size();
	uint16_t method = MethodFor(options, data.size());
	Bytes packed;
	if (method != METHOD_STORE) {
		auto target = std::make_shared<Bytes>();
		auto sink = OpenMemorySink(target);
		if (sink.IsError())
			return Err(sink.Error());
		auto encoder =
			OpenMethodEncoder(std::move(sink).Unwrap(), method, options, data.size());
		if (encoder.IsError())
			return Err(encoder.Error());
		if (auto written = StreamWrite(encoder.Value(), data); written.IsError())
			return written;
		if (auto finished = FinishStream(encoder.Value()); finished.IsError())
			return finished;
		if (target->size() < data.size())
			packed = std::move(*target);
		else
			method = METHOD_STORE;
	}
	const Bytes& payload = method == METHOD_STORE ? data : packed;
	SetMethod(method, central);
	MarkEncryption(options, central);
	const uint64_t overhead = options.encryption == Encryption::AES256		 ? 16 + 2 + 10
							  : options.encryption == Encryption::ZIP_CRYPTO ? 12
																			 : 0;
	record.compressedSize = payload.size() + overhead;
	WriteLocalHeader(writer, central, false);
	if (!writer.Ok())
		return Err(MakeError(ErrorKind::IO, String("zip : écriture impossible")));
	if (options.encryption == Encryption::NONE) {
		writer.Write(payload);
		return writer.Ok()
				   ? Result<bool, ArchiveError>(Ok(true))
				   : Result<bool, ArchiveError>(
						 Err(MakeError(ErrorKind::IO, String("zip : écriture impossible"))));
	}
	auto sink = OpenBorrowedStream(output.stream);
	if (sink.IsError())
		return Err(sink.Error());
	auto encrypted =
		OpenEncryption(std::move(sink).Unwrap(), options, uint8_t(record.crc >> 24));
	if (encrypted.IsError())
		return Err(encrypted.Error());
	if (auto written = StreamWrite(encrypted.Value(), payload); written.IsError())
		return written;
	return FinishStream(encrypted.Value());
}

void ZipWriter::WriteLocalHeader(BinaryWriter& writer, const Central& central, bool zip64) {
	using namespace detail::zip;
	const Record& record = central.record;
	const bool descriptor = (record.flags & FLAG_DATA_DESCRIPTOR) != 0;
	Bytes extra = BuildExtra(record, central.modifiedTime, zip64, false);
	writer.U32Le(LOCAL_SIGNATURE);
	writer.U16Le(central.versionNeeded);
	writer.U16Le(record.flags);
	writer.U16Le(record.aes ? METHOD_AES : record.method);
	writer.U16Le(record.dosTime);
	writer.U16Le(record.dosDate);
	writer.U32Le(record.aes || descriptor ? 0 : record.crc);
	writer.U32Le(zip64 ? 0xFFFFFFFFu : descriptor ? 0 : uint32_t(record.compressedSize));
	writer.U32Le(zip64 ? 0xFFFFFFFFu : descriptor ? 0 : uint32_t(record.uncompressedSize));
	writer.U16Le(uint16_t(central.name.GetSize()));
	writer.U16Le(uint16_t(extra.size()));
	writer.WriteString(central.name);
	writer.Write(extra);
}

uint16_t ZipWriter::MethodFor(const WriteOptions& options, uint64_t size) noexcept {
	using namespace detail::zip;
	if (size == 0 || options.level == 0)
		return METHOD_STORE;
	switch (options.compression) {
	case Compression::STORE:
		return METHOD_STORE;
	case Compression::DEFLATE:
		return METHOD_DEFLATE;
	case Compression::LZMA:
		return METHOD_LZMA;
	case Compression::LZMA2:
		return METHOD_XZ;
	case Compression::BZIP2:
		return METHOD_BZIP2;
	case Compression::ZSTD:
		return METHOD_ZSTD;
	case Compression::PPMD:
		return METHOD_PPMD;
	}
	return METHOD_STORE;
}

void ZipWriter::SetMethod(uint16_t method, Central& central) {
	using namespace detail::zip;
	central.record.method = method;
	central.versionNeeded = std::max(central.versionNeeded, VersionFor(method));
	// LZMA toujours terminé par un marqueur de fin (comme 7-Zip) : certains
	// lecteurs l'exigent quand un descripteur de données suit.
	if (method == METHOD_LZMA)
		central.record.flags |= FLAG_LZMA_END_MARKER;
}

uint16_t ZipWriter::VersionFor(uint16_t method) noexcept {
	using namespace detail::zip;
	switch (method) {
	case METHOD_STORE:
	case METHOD_DEFLATE:
		return 20;
	case METHOD_BZIP2:
		return 46;
	default:
		return 63;
	}
}

Result<ArchiveStream, ArchiveError> ZipWriter::OpenMethodEncoder(ArchiveStream sink, uint16_t method, const WriteOptions& options,
		uint64_t sizeHint) {
	using namespace detail::zip;
	const int level = std::clamp(options.level, 1, 9);
	switch (method) {
	case METHOD_DEFLATE:
		return OpenDeflateEncoder(std::move(sink), level);
	case METHOD_LZMA: {
		LzmaProperties props;
		props.dictionarySize = uint32_t(
			std::min<uint64_t>(options.dictionarySize, std::max<uint64_t>(sizeHint, 4096)));
		const auto encoded = props.Encode();
		// Version du SDK LZMA annoncée (16.04), taille des propriétés.
		const uint8_t header[4 + 5] = {
			16, 4, 5, 0, encoded[0], encoded[1], encoded[2], encoded[3], encoded[4]};
		if (auto written = StreamWrite(sink, header); written.IsError())
			return Err(written.Error());
		return OpenLzmaEncoder(std::move(sink), props, level, true, sizeHint);
	}
	case METHOD_XZ:
		return OpenXzEncoder(std::move(sink), level, options.dictionarySize, XzCheck::CRC32,
							 sizeHint);
	case METHOD_BZIP2:
		return OpenBzip2Encoder(std::move(sink), level);
	case METHOD_ZSTD:
		return OpenZstdEncoder(std::move(sink), level);
	case METHOD_PPMD:
		return OpenPpmd8ZipEncoder(std::move(sink), std::clamp(options.ppmdOrder, 2u, 16u),
								   std::clamp<uint32_t>(options.ppmdMemoryMb, 1, 256));
	default:
		return Ok(std::move(sink));
	}
}

void ZipWriter::MarkEncryption(const WriteOptions& options, Central& central) {
	using namespace detail::zip;
	Record& record = central.record;
	if (options.encryption == Encryption::NONE)
		return;
	record.flags |= FLAG_ENCRYPTED;
	if (options.encryption == Encryption::AES256) {
		record.aes = true;
		record.aesVersion = 2;
		record.aesStrength = 3;
		central.versionNeeded = std::max<uint16_t>(central.versionNeeded, 51);
	}
}

Result<ArchiveStream, ArchiveError> ZipWriter::OpenEncryption(ArchiveStream sink, const WriteOptions& options, uint8_t checkByte) {
	using namespace detail::zip;
	if (options.encryption == Encryption::NONE)
		return Ok(std::move(sink));
	auto state = std::make_shared<StreamState>();
	if (options.encryption == Encryption::ZIP_CRYPTO) {
		auto header = SecureRandomBytes(12);
		if (header.IsError())
			return Err(MakeError(ErrorKind::IO, header.Error()));
		Bytes h = std::move(header).Unwrap();
		h[11] = checkByte;
		return MakeStream(std::make_unique<ZipCryptoEncryptImpl>(std::move(sink), std::move(h),
																 options.password, state),
						  state);
	}
	static constexpr size_t KEY_LENGTH = 32, SALT_LENGTH = 16;
	auto salt = SecureRandomBytes(SALT_LENGTH);
	if (salt.IsError())
		return Err(MakeError(ErrorKind::IO, salt.Error()));
	const Bytes keys =
		Pbkdf2HmacSha1(PasswordBytes(options.password), salt.Value(), 1000, 2 * KEY_LENGTH + 2);
	auto aes = Aes::Create(std::span<const uint8_t>(keys).subspan(0, KEY_LENGTH));
	if (aes.IsError())
		return Err(MakeError(ErrorKind::IO, aes.Error()));
	const Hmac<Sha1> hmac(std::span<const uint8_t>(keys).subspan(KEY_LENGTH, KEY_LENGTH));
	Bytes header = std::move(salt).Unwrap();
	header.push_back(keys[2 * KEY_LENGTH]);
	header.push_back(keys[2 * KEY_LENGTH + 1]);
	return MakeStream(std::make_unique<AesEncryptImpl>(std::move(sink), std::move(header),
													   aes.Value(), hmac, state),
					  state);
}

Bytes ZipWriter::BuildExtra(const detail::zip::Record& record, int64_t modifiedTime, bool sizes64, bool offset64) {
	auto stream = CreateMemoryWriter();
	if (stream.IsError())
		return {};
	BinaryWriter writer(stream.Value());
	if (sizes64 || offset64) {
		writer.U16Le(0x0001);
		writer.U16Le(uint16_t((sizes64 ? 16 : 0) + (offset64 ? 8 : 0)));
		if (sizes64) {
			writer.U64Le(record.uncompressedSize);
			writer.U64Le(record.compressedSize);
		}
		if (offset64)
			writer.U64Le(record.localOffset);
	}
	if (modifiedTime > 0 && modifiedTime <= 0xFFFFFFFFll) {
		writer.U16Le(0x5455);
		writer.U16Le(5);
		writer.U8(0x01);
		writer.U32Le(uint32_t(modifiedTime));
	}
	if (record.aes) {
		writer.U16Le(0x9901);
		writer.U16Le(7);
		writer.U16Le(record.aesVersion);
		writer.U8('A');
		writer.U8('E');
		writer.U8(record.aesStrength);
		writer.U16Le(record.method);
	}
	return stream.Value().DynamicMemoryBytes();
}

} // namespace data::archive
