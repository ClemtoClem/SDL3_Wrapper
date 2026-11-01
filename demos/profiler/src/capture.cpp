// Définitions de profiler/capture.hpp
#include <profiler/capture.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace profiler {

namespace {

/// Lecteur JSON minimal, sans arbre : chaque section de la capture est lue
/// directement dans ses vecteurs.
class Reader {
public:
	explicit Reader(StringView text) : m_begin(text.GetData()), m_p(text.GetData()), m_end(text.GetData() + text.GetSize()) {}

	[[nodiscard]] bool Failed() const noexcept { return !m_error.IsEmpty(); }
	[[nodiscard]] const String &Error() const noexcept { return m_error; }

	void Ws() {
		while (m_p < m_end && (*m_p == ' ' || *m_p == '\n' || *m_p == '\r' || *m_p == '\t'))
			++m_p;
	}
	bool Peek(char c) {
		Ws();
		return m_p < m_end && *m_p == c;
	}
	bool Accept(char c) {
		if (!Peek(c))
			return false;
		++m_p;
		return true;
	}
	void Expect(char c) {
		if (!Accept(c))
			Fail(String::Format("« %c » attendu", c));
	}
	void Fail(const String &message) {
		if (m_error.IsEmpty())
			m_error = String::Format("%s (octet %zu)", message.CStr(), size_t(m_p - m_begin));
		m_p = m_end;
	}

	String Str() {
		String out;
		Ws();
		if (m_p >= m_end || *m_p != '"') {
			Fail(String("chaîne attendue"));
			return out;
		}
		++m_p;
		while (m_p < m_end && *m_p != '"') {
			if (*m_p == '\\' && m_p + 1 < m_end) {
				++m_p;
				const char c = *m_p;
				out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
			} else {
				out += *m_p;
			}
			++m_p;
		}
		if (m_p < m_end)
			++m_p;
		return out;
	}

	double Num() {
		Ws();
		char *after = nullptr;
		const double v = std::strtod(m_p, &after);
		if (after == m_p) {
			Fail(String("nombre attendu"));
			return 0.0;
		}
		m_p = after;
		return v;
	}

	/// Entier non signé exact (adresses, compteurs).
	uint64_t U64() {
		Ws();
		const char *start = m_p;
		char *after = nullptr;
		const unsigned long long v = std::strtoull(m_p, &after, 10);
		if (after == m_p) {
			Fail(String("entier attendu"));
			return 0;
		}
		m_p = after;
		if (m_p < m_end && (*m_p == '.' || *m_p == 'e' || *m_p == 'E')) { // écrit en flottant : on le relit
			m_p = start;
			return uint64_t(Num());
		}
		return v;
	}

	bool Bool() {
		Ws();
		if (m_end - m_p >= 4 && std::strncmp(m_p, "true", 4) == 0) {
			m_p += 4;
			return true;
		}
		if (m_end - m_p >= 5 && std::strncmp(m_p, "false", 5) == 0) {
			m_p += 5;
			return false;
		}
		Fail(String("booléen attendu"));
		return false;
	}

	template <typename Fn> void Array(Fn &&element) {
		Expect('[');
		if (Accept(']'))
			return;
		do {
			element();
		} while (!Failed() && Accept(','));
		Expect(']');
	}

	template <typename Fn> void Object(Fn &&member) {
		Expect('{');
		if (Accept('}'))
			return;
		do {
			const String key = Str();
			Expect(':');
			member(key);
		} while (!Failed() && Accept(','));
		Expect('}');
	}

	void Skip() {
		Ws();
		if (m_p >= m_end)
			return;
		if (*m_p == '{')
			Object([&](const String &) { Skip(); });
		else if (*m_p == '[')
			Array([&] { Skip(); });
		else if (*m_p == '"')
			(void)Str();
		else if (*m_p == 't' || *m_p == 'f')
			(void)Bool();
		else if (m_end - m_p >= 4 && std::strncmp(m_p, "null", 4) == 0)
			m_p += 4;
		else
			(void)Num();
	}

private:
	const char *m_begin;
	const char *m_p;
	const char *m_end;
	String m_error;
};

} // namespace

String Capture::ThreadName(long tid) const {
	for (const ThreadInfo &t : threads)
		if (t.tid == tid && !t.name.IsEmpty()) {
			// Un fil non nommé hérite du nom de son créateur : homonymes
			// distingués par leur numéro (le fil principal garde le sien).
			size_t same = 0;
			for (const ThreadInfo &o : threads)
				same += o.name == t.name ? 1u : 0u;
			return same > 1 && tid != pid ? String::Format("%s #%ld", t.name.CStr(), tid) : t.name;
		}
	return String::Format("fil %ld", tid);
}

const String &Capture::Name(uint32_t index) const {
	static const String UNKNOWN("?");
	return index < names.size() ? names[index] : UNKNOWN;
}

std::vector<long> Capture::ThreadIds() const {
	std::set<long> ids;
	for (const ThreadInfo &t : threads)
		ids.insert(t.tid);
	for (const ZoneEvent &z : zones)
		ids.insert(z.tid);
	for (const StackSample &s : samples)
		ids.insert(s.tid);
	for (const TaskSample &t : tasks)
		ids.insert(t.tid);
	// Le fil principal (même numéro que le processus) d'abord.
	std::vector<long> out(ids.begin(), ids.end());
	std::stable_partition(out.begin(), out.end(), [this](long tid) { return tid == pid; });
	return out;
}

Result<Capture, String> ParseCapture(StringView text, const String &path) {
	Capture c;
	c.path = path;
	Reader r(text);
	bool formatOk = false;
	r.Object([&](const String &key) {
		if (key == "format") {
			formatOk = r.Str() == "diagnostics-capture";
		} else if (key == "pid") {
			c.pid = long(r.Num());
		} else if (key == "duration_ns") {
			c.durationNs = int64_t(r.Num());
		} else if (key == "sampling_hz") {
			c.samplingHz = int(r.Num());
		} else if (key == "process_hz") {
			c.processHz = int(r.Num());
		} else if (key == "executable") {
			c.executable = r.Str();
		} else if (key == "threads") {
			r.Array([&] {
				ThreadInfo t;
				r.Object([&](const String &k) {
					if (k == "tid")
						t.tid = long(r.Num());
					else if (k == "name")
						t.name = r.Str();
					else
						r.Skip();
				});
				c.threads.push_back(std::move(t));
			});
		} else if (key == "modules") {
			r.Array([&] {
				Module m;
				r.Object([&](const String &k) {
					if (k == "start")
						m.start = r.U64();
					else if (k == "end")
						m.end = r.U64();
					else if (k == "offset")
						m.offset = r.U64();
					else if (k == "path")
						m.path = r.Str();
					else
						r.Skip();
				});
				c.modules.push_back(std::move(m));
			});
		} else if (key == "zones") {
			r.Array([&] {
				ZoneEvent z;
				r.Expect('[');
				z.tid = long(r.Num());
				r.Expect(',');
				z.name = uint32_t(r.Num());
				r.Expect(',');
				z.start = int64_t(r.Num());
				r.Expect(',');
				z.end = int64_t(r.Num());
				r.Expect(',');
				z.depth = uint32_t(r.Num());
				r.Expect(']');
				c.zones.push_back(z);
			});
		} else if (key == "values") {
			r.Array([&] {
				ValueEvent v;
				r.Expect('[');
				v.tid = long(r.Num());
				r.Expect(',');
				v.name = uint32_t(r.Num());
				r.Expect(',');
				v.time = int64_t(r.Num());
				r.Expect(',');
				v.value = r.Num();
				r.Expect(']');
				c.values.push_back(v);
			});
		} else if (key == "async") {
			r.Array([&] {
				AsyncEvent a;
				r.Expect('[');
				a.tid = long(r.Num());
				r.Expect(',');
				a.name = uint32_t(r.Num());
				r.Expect(',');
				a.id = r.U64();
				r.Expect(',');
				a.time = int64_t(r.Num());
				r.Expect(',');
				a.begin = r.Num() != 0.0;
				r.Expect(']');
				c.async.push_back(a);
			});
		} else if (key == "frames") {
			r.Array([&] { c.frames.push_back(int64_t(r.Num())); });
		} else if (key == "samples") {
			r.Array([&] {
				StackSample s;
				r.Expect('[');
				s.tid = long(r.Num());
				r.Expect(',');
				s.time = int64_t(r.Num());
				s.first = uint32_t(c.stackFrames.size());
				while (r.Accept(','))
					c.stackFrames.push_back(r.U64());
				s.depth = uint32_t(c.stackFrames.size()) - s.first;
				r.Expect(']');
				c.samples.push_back(s);
			});
		} else if (key == "tasks") {
			r.Array([&] {
				TaskSample t;
				r.Expect('[');
				t.tid = long(r.Num());
				r.Expect(',');
				t.time = int64_t(r.Num());
				r.Expect(',');
				t.cpuNs = r.U64();
				r.Expect(',');
				const String state = r.Str();
				t.state = state.IsEmpty() ? '?' : state[0];
				r.Expect(']');
				c.tasks.push_back(t);
			});
		} else if (key == "process") {
			r.Array([&] {
				ProcessSample p;
				r.Expect('[');
				p.time = int64_t(r.Num());
				r.Expect(',');
				p.rss = r.U64();
				r.Expect(',');
				p.heap = r.U64();
				r.Expect(',');
				p.minorFaults = long(r.Num());
				r.Expect(',');
				p.majorFaults = long(r.Num());
				r.Expect(',');
				p.voluntarySwitches = long(r.Num());
				r.Expect(',');
				p.involuntarySwitches = long(r.Num());
				r.Expect(',');
				p.userNs = r.U64();
				r.Expect(',');
				p.systemNs = r.U64();
				r.Expect(']');
				c.process.push_back(p);
			});
		} else if (key == "hardware") {
			r.Object([&](const String &k) {
				if (k == "available") {
					c.hardwareAvailable = r.Bool();
				} else if (k == "reason") {
					c.hardwareReason = r.Str();
				} else if (k == "samples") {
					r.Array([&] {
						HardwareSample h;
						r.Expect('[');
						h.time = int64_t(r.Num());
						r.Expect(',');
						h.cycles = r.U64();
						r.Expect(',');
						h.instructions = r.U64();
						r.Expect(',');
						h.cacheReferences = r.U64();
						r.Expect(',');
						h.cacheMisses = r.U64();
						r.Expect(',');
						h.branchMisses = r.U64();
						r.Expect(']');
						c.hardware.push_back(h);
					});
				} else {
					r.Skip();
				}
			});
		} else if (key == "names") {
			r.Array([&] { c.names.push_back(r.Str()); });
		} else if (key == "zones_dropped") {
			c.zonesDropped = size_t(r.Num());
		} else if (key == "samples_dropped") {
			c.samplesDropped = size_t(r.Num());
		} else {
			r.Skip();
		}
	});
	if (r.Failed())
		return Err(String::Format("%s : capture illisible — %s", path.CStr(), r.Error().CStr()));
	if (!formatOk)
		return Err(String::Format("%s : ce n'est pas une capture diagnostics::trace", path.CStr()));
	std::stable_sort(c.zones.begin(), c.zones.end(), [](const ZoneEvent &a, const ZoneEvent &b) {
		return a.tid != b.tid ? a.tid < b.tid : a.start != b.start ? a.start < b.start : a.depth < b.depth;
	});
	std::stable_sort(c.tasks.begin(), c.tasks.end(), [](const TaskSample &a, const TaskSample &b) {
		return a.tid != b.tid ? a.tid < b.tid : a.time < b.time;
	});
	std::sort(c.frames.begin(), c.frames.end());
	return Ok(std::move(c));
}

Result<Capture, String> LoadCapture(const String &path) {
	FILE *f = std::fopen(path.CStr(), "rb");
	if (!f)
		return Err(String::Format("%s : %s", path.CStr(), std::strerror(errno)));
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	std::vector<char> buffer(size_t(std::max(0L, size)) + 1, '\0');
	const size_t read = std::fread(buffer.data(), 1, size_t(std::max(0L, size)), f);
	std::fclose(f);
	return ParseCapture(StringView(buffer.data(), read), path);
}

} // namespace profiler
