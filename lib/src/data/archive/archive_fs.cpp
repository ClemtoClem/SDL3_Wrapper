// Définitions de data/archive/archive_fs.hpp
#include "data/archive/archive_fs.hpp"

namespace data::archive {

Result<String, ArchiveError> NormalizePath(const String& raw) {
	std::vector<String> parts;
	String current;
	auto flush = [&parts, &current]() -> bool {
		if (current.IsEmpty() || current == ".") {
			current.Clear();
			return true;
		}
		if (current == "..") {
			if (parts.empty())
				return false;
			parts.pop_back();
		} else {
			parts.push_back(current);
		}
		current.Clear();
		return true;
	};
	for (size_t i = 0; i < raw.GetSize(); ++i) {
		char c = raw.CharAt(i);
		if (c == '\0')
			return Err(MakeError(ErrorKind::UNSAFE_PATH, String("chemin contenant un octet nul")));
		if (c == '/' || c == '\\') {
			if (!flush())
				return Err(MakeError(ErrorKind::UNSAFE_PATH,
									 String::Format("« %s » sort de la racine", raw.CStr())));
		} else {
			current.PushBack(c);
		}
	}
	if (!flush())
		return Err(MakeError(ErrorKind::UNSAFE_PATH,
							 String::Format("« %s » sort de la racine", raw.CStr())));
	// Lettre de lecteur Windows (« C: ») en tête : chemin absolu déguisé.
	if (!parts.empty() && parts.front().GetSize() == 2 && parts.front().CharAt(1) == ':')
		return Err(MakeError(ErrorKind::UNSAFE_PATH,
							 String::Format("« %s » est un chemin absolu", raw.CStr())));
	return Ok(String::Join(parts, "/"));
}

String JoinPath(const String& directory, const String& name) {
	if (directory.IsEmpty())
		return name;
	if (name.IsEmpty())
		return directory;
	return directory.EndsWith("/") ? directory + name : directory + "/" + name;
}

String ParentPath(const String& path) {
	size_t slash = path.Rfind('/');
	return slash == String::NPOS ? String() : path.Substr(0, slash);
}

String BaseName(const String& path) {
	size_t slash = path.Rfind('/');
	return slash == String::NPOS ? path : path.Substr(slash + 1);
}

namespace detail::glob {

bool MatchSegment(const char* pattern, size_t pLength, const char* text, size_t tLength) {
	size_t p = 0, t = 0, starP = SIZE_MAX, starT = 0;
	while (t < tLength) {
		if (p < pLength && pattern[p] == '*') {
			starP = p++;
			starT = t;
			continue;
		}
		if (p < pLength && pattern[p] == '[') {
			size_t close = p + 1;
			bool negate = close < pLength && (pattern[close] == '!' || pattern[close] == '^');
			if (negate)
				++close;
			size_t first = close;
			while (close < pLength && (pattern[close] != ']' || close == first))
				++close;
			if (close < pLength) {
				bool found = false;
				for (size_t k = first; k < close; ++k) {
					if (k + 2 < close && pattern[k + 1] == '-') {
						found = found || (text[t] >= pattern[k] && text[t] <= pattern[k + 2]);
						k += 2;
					} else {
						found = found || text[t] == pattern[k];
					}
				}
				if (found != negate) {
					p = close + 1;
					++t;
					continue;
				}
			}
		} else if (p < pLength && (pattern[p] == '?' || pattern[p] == text[t])) {
			++p;
			++t;
			continue;
		}
		if (starP == SIZE_MAX)
			return false;
		p = starP + 1;
		t = ++starT;
	}
	while (p < pLength && pattern[p] == '*')
		++p;
	return p == pLength;
}

bool MatchParts(const std::vector<String>& pattern, size_t p, const std::vector<String>& path, size_t t) {
	if (p == pattern.size())
		return t == path.size();
	if (pattern[p] == "**") {
		for (size_t skip = t; skip <= path.size(); ++skip)
			if (MatchParts(pattern, p + 1, path, skip))
				return true;
		return false;
	}
	if (t == path.size())
		return false;
	return MatchSegment(pattern[p].CStr(), pattern[p].GetSize(), path[t].CStr(),
						path[t].GetSize()) &&
		   MatchParts(pattern, p + 1, path, t + 1);
}

} // namespace detail::glob

bool MatchGlob(const String& pattern, const String& path) {
	return detail::glob::MatchParts(pattern.Split('/'), 0,
									path.IsEmpty() ? std::vector<String>{} : path.Split('/'), 0);
}

// ── VirtualFs::Node ──────────────────────────────────────────────────────────

bool VirtualFs::Node::IsDirectory() const noexcept {
	return info.type == EntryType::DIRECTORY;
}

// ── VirtualFs ────────────────────────────────────────────────────────────────

VirtualFs::VirtualFs() {
	Node root;
	root.info.type = EntryType::DIRECTORY;
	m_nodes.push_back(std::move(root));
}

Result<VirtualFs::NodeId, ArchiveError> VirtualFs::AddFile(const String& path, Bytes content,
		int64_t modifiedTime, uint32_t mode) {
	EntryInfo info;
	info.path = path;
	info.type = EntryType::FILE;
	info.size = content.size();
	info.modifiedTime = modifiedTime;
	info.mode = mode;
	auto id = Insert(std::move(info));
	if (id.IsOk())
		m_nodes[id.Value()].content = std::move(content);
	return id;
}

Result<VirtualFs::NodeId, ArchiveError> VirtualFs::AddLazyFile(const String& path, uint64_t size, Opener opener,
		int64_t modifiedTime, uint32_t mode) {
	EntryInfo info;
	info.path = path;
	info.type = EntryType::FILE;
	info.size = size;
	info.modifiedTime = modifiedTime;
	info.mode = mode;
	auto id = Insert(std::move(info));
	if (id.IsOk())
		m_nodes[id.Value()].opener = std::move(opener);
	return id;
}

Result<VirtualFs::NodeId, ArchiveError> VirtualFs::AddDirectory(const String& path, int64_t modifiedTime, uint32_t mode) {
	EntryInfo info;
	info.path = path;
	info.type = EntryType::DIRECTORY;
	info.modifiedTime = modifiedTime;
	info.mode = mode;
	return Insert(std::move(info));
}

Result<VirtualFs::NodeId, ArchiveError> VirtualFs::AddSymlink(const String& path, const String& target,
		int64_t modifiedTime) {
	EntryInfo info;
	info.path = path;
	info.type = EntryType::SYMLINK;
	info.linkTarget = target;
	info.size = target.GetSize();
	info.modifiedTime = modifiedTime;
	info.mode = 0777;
	return Insert(std::move(info));
}

Result<VirtualFs::NodeId, ArchiveError> VirtualFs::AddEntry(const EntryInfo& entry, size_t entryIndex, Opener opener) {
	auto id = Insert(entry);
	if (id.IsOk()) {
		m_nodes[id.Value()].entryIndex = Some(entryIndex);
		m_nodes[id.Value()].opener = std::move(opener);
	}
	return id;
}

Result<size_t, ArchiveError> VirtualFs::AddDiskPath(const String& diskPath, const String& archivePath,
		bool recursive, bool followSymlinks) {
	String target = archivePath.IsEmpty() ? BaseName(diskPath) : archivePath;
	// Lien symbolique : archivé comme lien (sa cible telle quelle), sauf
	// si l'on demande de le suivre. SDL_GetPathInfo, lui, suit toujours.
	if (!followSymlinks && platform::IsSymlink(diskPath)) {
		Option<String> link = platform::ReadSymlink(diskPath);
		if (link.IsNone())
			return Err(MakeError(ErrorKind::IO, String::Format("lecture du lien %s impossible",
															   diskPath.CStr())));
		auto added = AddSymlink(target, link.Unwrap());
		if (added.IsError())
			return Err(added.Error());
		return Ok(size_t(1));
	}
	Option<sdl3::PathInfo> info = sdl3::filesystem::PathInfo(diskPath);
	if (info.IsNone())
		return Err(MakeError(ErrorKind::IO, String::Format("%s introuvable", diskPath.CStr())));
	const int64_t modified = int64_t(info.Value().modifyTime / 1000000000LL);
	if (info.Value().type == sdl3::PathType::FILE) {
		String source = diskPath;
		auto added = AddLazyFile(
			target, info.Value().size, [source]() { return OpenFileStream(source); }, modified);
		if (added.IsError())
			return Err(added.Error());
		return Ok(size_t(1));
	}
	if (info.Value().type != sdl3::PathType::DIRECTORY)
		return Ok(size_t(0)); // périphériques, sockets : ignorés
	size_t added = 0;
	if (!target.IsEmpty()) {
		auto directory = AddDirectory(target, modified);
		if (directory.IsError())
			return Err(directory.Error());
		++added;
	}
	std::vector<String> names;
	(void)sdl3::filesystem::EnumerateDirectory(diskPath,
											   [&names](const char*, const char* name) {
												   names.push_back(String(name));
												   return true;
											   });
	std::sort(names.begin(), names.end(),
			  [](const String& a, const String& b) { return a.Compare(b) < 0; });
	for (const String& name : names) {
		String child = JoinPath(diskPath, name);
		const bool link = !followSymlinks && platform::IsSymlink(child);
		Option<sdl3::PathInfo> childInfo = sdl3::filesystem::PathInfo(child);
		if (childInfo.IsNone() && !link)
			continue; // lien cassé suivi, entrée disparue
		if (!link && childInfo.Value().type == sdl3::PathType::DIRECTORY && !recursive)
			continue;
		auto count = AddDiskPath(child, JoinPath(target, name), recursive, followSymlinks);
		if (count.IsError())
			return count;
		added += count.Value();
	}
	return Ok(added);
}

bool VirtualFs::Remove(const String& path) {
	Option<NodeId> id = Find(path);
	if (id.IsNone() || id.Unwrap() == ROOT)
		return false;
	Node& parent = m_nodes[m_nodes[id.Unwrap()].parent];
	std::erase(parent.children, id.Unwrap());
	DetachSubtree(id.Unwrap());
	return true;
}

Option<VirtualFs::NodeId> VirtualFs::Find(const String& path) const {
	auto normalized = NormalizePath(path);
	if (normalized.IsError())
		return NONE;
	NodeId current = ROOT;
	if (normalized.Value().IsEmpty())
		return Some(current);
	for (const String& part : normalized.Value().Split('/')) {
		Option<NodeId> child = ChildNamed(current, part);
		if (child.IsNone())
			return NONE;
		current = child.Unwrap();
	}
	return Some(current);
}

const std::vector<VirtualFs::NodeId>& VirtualFs::Children(NodeId id) const noexcept {
	return m_nodes[id].children;
}

void VirtualFs::Walk(const std::function<bool(NodeId, int)>& visit, NodeId from) const {
	WalkFrom(from, 0, visit);
}

std::vector<VirtualFs::NodeId> VirtualFs::Flatten(NodeId from) const {
	std::vector<NodeId> nodes;
	Walk(
		[&nodes](NodeId id, int) {
			if (id != ROOT)
				nodes.push_back(id);
			return true;
		},
		from);
	return nodes;
}

std::vector<VirtualFs::NodeId> VirtualFs::Glob(const String& pattern) const {
	std::vector<NodeId> matches;
	for (NodeId id : Flatten())
		if (MatchGlob(pattern, m_nodes[id].info.path))
			matches.push_back(id);
	return matches;
}

size_t VirtualFs::FileCount() const {
	size_t count = 0;
	for (NodeId id : Flatten())
		count += m_nodes[id].IsFile() ? 1 : 0;
	return count;
}

uint64_t VirtualFs::TotalSize() const {
	uint64_t total = 0;
	for (NodeId id : Flatten())
		if (m_nodes[id].IsFile())
			total += m_nodes[id].info.size;
	return total;
}

Result<ArchiveStream, ArchiveError> VirtualFs::OpenContent(NodeId id) const {
	const Node& node = m_nodes[id];
	if (node.info.type == EntryType::SYMLINK) {
		const String& target = node.info.linkTarget;
		return OpenViewStream(std::span<const uint8_t>(
			reinterpret_cast<const uint8_t*>(target.CStr()), target.GetSize()));
	}
	if (!node.IsFile())
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT,
							 String::Format("%s n'est pas un fichier", node.info.path.CStr())));
	if (node.opener)
		return node.opener();
	return OpenViewStream(node.content);
}

Result<Bytes, ArchiveError> VirtualFs::ReadContent(NodeId id, uint64_t limit) const {
	const Node& node = m_nodes[id];
	if (node.IsFile() && !node.opener)
		return Ok(node.content);
	auto stream = OpenContent(id);
	if (stream.IsError())
		return Err(stream.Error());
	return ReadStreamToEnd(stream.Value(), limit, node.info.size);
}

String VirtualFs::Tree(NodeId from, int maxDepth) const {
	String out;
	out.Concat(from == ROOT ? String("/") : m_nodes[from].info.path);
	out.Concat("\n");
	AppendTree(out, from, String(), maxDepth);
	return out;
}

Option<VirtualFs::NodeId> VirtualFs::ChildNamed(NodeId parent, const String& name) const {
	const auto& children = m_nodes[parent].children;
	auto it = std::lower_bound(
		children.begin(), children.end(), name,
		[this](NodeId id, const String& value) { return m_nodes[id].name.Compare(value) < 0; });
	if (it != children.end() && m_nodes[*it].name == name)
		return Some(*it);
	return NONE;
}

Result<VirtualFs::NodeId, ArchiveError> VirtualFs::Insert(EntryInfo info) {
	auto normalized = NormalizePath(info.path);
	if (normalized.IsError())
		return Err(normalized.Error());
	if (normalized.Value().IsEmpty())
		return info.type == EntryType::DIRECTORY
				   ? Result<NodeId, ArchiveError>(Ok(ROOT))
				   : Result<NodeId, ArchiveError>(
						 Err(MakeError(ErrorKind::INVALID_ARGUMENT, String("chemin vide"))));
	info.path = normalized.Value();
	std::vector<String> parts = info.path.Split('/');
	NodeId current = ROOT;
	String prefix;
	for (size_t i = 0; i + 1 < parts.size(); ++i) {
		prefix = JoinPath(prefix, parts[i]);
		Option<NodeId> child = ChildNamed(current, parts[i]);
		if (child.IsSome()) {
			if (!m_nodes[child.Unwrap()].IsDirectory())
				return Err(
					MakeError(ErrorKind::CORRUPT,
							  String::Format("« %s » est à la fois un fichier et un dossier",
											 prefix.CStr())));
			current = child.Unwrap();
			continue;
		}
		EntryInfo directory;
		directory.path = prefix;
		directory.type = EntryType::DIRECTORY;
		directory.mode = 0755;
		current = CreateChild(current, parts[i], std::move(directory));
		m_nodes[current].implicit = true;
	}
	const String& leaf = parts.back();
	Option<NodeId> existing = ChildNamed(current, leaf);
	if (existing.IsSome()) {
		Node& node = m_nodes[existing.Unwrap()];
		if (node.IsDirectory() && info.type == EntryType::DIRECTORY) {
			// Dossier implicite devenu explicite : garder ses enfants.
			node.implicit = false;
			node.info.modifiedTime =
				info.modifiedTime != 0 ? info.modifiedTime : node.info.modifiedTime;
			node.info.mode = info.mode != 0 ? info.mode : node.info.mode;
			return Ok(existing.Unwrap());
		}
		if (node.IsDirectory() != (info.type == EntryType::DIRECTORY))
			return Err(MakeError(ErrorKind::CORRUPT,
								 String::Format("« %s » est à la fois un fichier et un dossier",
												info.path.CStr())));
		// Doublon de fichier : la dernière occurrence l'emporte (zip, tar).
		node.info = std::move(info);
		node.content.clear();
		node.opener = {};
		node.entryIndex = NONE;
		return Ok(existing.Unwrap());
	}
	return Ok(CreateChild(current, leaf, std::move(info)));
}

VirtualFs::NodeId VirtualFs::CreateChild(NodeId parent, const String& name, EntryInfo info) {
	Node node;
	node.name = name;
	node.parent = parent;
	node.info = std::move(info);
	NodeId id = NodeId(m_nodes.size());
	m_nodes.push_back(std::move(node));
	auto& children = m_nodes[parent].children;
	auto it = std::lower_bound(children.begin(), children.end(), name,
							   [this](NodeId child, const String& value) {
								   return m_nodes[child].name.Compare(value) < 0;
							   });
	children.insert(it, id);
	return id;
}

void VirtualFs::DetachSubtree(NodeId id) {
	for (NodeId child : m_nodes[id].children)
		DetachSubtree(child);
	m_nodes[id].children.clear();
	m_nodes[id].content.clear();
	m_nodes[id].opener = {};
	m_nodes[id].name.Clear();
}

bool VirtualFs::WalkFrom(NodeId id, int depth, const std::function<bool(NodeId, int)>& visit) const {
	if (!visit(id, depth))
		return true;
	for (NodeId child : m_nodes[id].children)
		(void)WalkFrom(child, depth + 1, visit);
	return true;
}

void VirtualFs::AppendTree(String& out, NodeId id, const String& indent, int depth) const {
	if (depth <= 0)
		return;
	const auto& children = m_nodes[id].children;
	for (size_t i = 0; i < children.size(); ++i) {
		const Node& child = m_nodes[children[i]];
		const bool last = i + 1 == children.size();
		out.Concat(indent);
		out.Concat(last ? "└── " : "├── ");
		out.Concat(child.name);
		if (child.IsDirectory())
			out.Concat("/");
		else if (child.info.type == EntryType::SYMLINK)
			out.Concat(String::Format(" -> %s", child.info.linkTarget.CStr()));
		else
			out.Concat(
				String::Format(" (%llu)", static_cast<unsigned long long>(child.info.size)));
		out.Concat("\n");
		if (child.IsDirectory())
			AppendTree(out, children[i], indent + (last ? "    " : "│   "), depth - 1);
	}
}

// ── Navigator ────────────────────────────────────────────────────────────────

VirtualFs::NodeId Navigator::Current() const {
	return m_fs->Find(m_current).UnwrapOr(VirtualFs::ROOT);
}

Option<VirtualFs::NodeId> Navigator::Resolve(const String& path) const {
	auto absolute = Absolute(path);
	if (absolute.IsError())
		return NONE;
	return m_fs->Find(absolute.Value());
}

Result<String, ArchiveError> Navigator::Absolute(const String& path) const {
	if (path.StartsWith("/"))
		return NormalizePath(path);
	return NormalizePath(JoinPath(m_current, path));
}

Result<bool, ArchiveError> Navigator::ChangeDirectory(const String& path) {
	auto absolute = Absolute(path.IsEmpty() ? String("/") : path);
	if (absolute.IsError())
		return Err(absolute.Error());
	Option<VirtualFs::NodeId> id = m_fs->Find(absolute.Value());
	if (id.IsNone())
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT,
							 String::Format("%s : dossier introuvable", path.CStr())));
	if (!m_fs->Get(id.Unwrap()).IsDirectory())
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT,
							 String::Format("%s n'est pas un dossier", path.CStr())));
	m_current = absolute.Value();
	return Ok(true);
}

std::vector<VirtualFs::NodeId> Navigator::List(const String& path) const {
	Option<VirtualFs::NodeId> id = path.IsEmpty() ? Some(Current()) : Resolve(path);
	if (id.IsNone())
		return {};
	if (!m_fs->Get(id.Unwrap()).IsDirectory())
		return {id.Unwrap()};
	return m_fs->Children(id.Unwrap());
}

std::vector<VirtualFs::NodeId> Navigator::Glob(const String& pattern) const {
	String absolute =
		pattern.StartsWith("/") ? pattern.Substr(1) : JoinPath(m_current, pattern);
	return m_fs->Glob(absolute);
}

} // namespace data::archive
