// Définitions de net/wifi.hpp
#include "net/wifi.hpp"

#include "sdl3/sdl3.hpp"

#include <algorithm>

namespace net::wifi {

namespace {

[[nodiscard]] String Lower(StringView s) {
	return String(s).ToLower();
}

[[nodiscard]] bool NeedsQuotes(const String& arg) {
	if (arg.IsEmpty())
		return true;
	for (char c : arg)
		if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' ||
			  c == '/' || c == ':' || c == '=' || c == ','))
			return true;
	return false;
}

[[nodiscard]] String Quote(const String& arg) {
	if (!NeedsQuotes(arg))
		return arg;
	return "'" + arg.Replace("'", "'\\''") + "'";
}

[[nodiscard]] int ToInt(const String& s) {
	return s.Trim().ToInt32();
}

/// « 2462 MHz » → 2462.
[[nodiscard]] int LeadingNumber(const String& s) {
	String digits;
	for (char c : s.Trim()) {
		if (c < '0' || c > '9')
			break;
		digits.Append(c);
	}
	return digits.IsEmpty() ? 0 : digits.ToInt32();
}

[[nodiscard]] WifiMode ParseMode(const String& s) {
	String m = s.ToLower();
	if (m.StartsWith("infra"))
		return WifiMode::INFRA;
	if (m.StartsWith("ad-hoc") || m.StartsWith("adhoc") || m == "ibss")
		return WifiMode::ADHOC;
	if (m.StartsWith("mesh"))
		return WifiMode::MESH;
	return WifiMode::OTHER;
}

/// Ligne « $ commande » puis la sortie, dans le journal d'un rapport.
void Log(ConfigureReport& report, const std::vector<String>& argv, const CommandResult* result) {
	report.log.push_back("$ " + CommandLine(argv));
	if (result && !result->output.Trim().IsEmpty())
		report.log.push_back(result->output.Trim());
}

/// Lance une étape ; false (rapport rempli) si elle échoue.
bool Step(ConfigureReport& report, const std::vector<String>& argv, const CommandRunner& run) {
	auto res = run(argv);
	if (!res) {
		Log(report, argv, nullptr);
		report.error = res.Error();
		report.log.push_back(report.error);
		return false;
	}
	Log(report, argv, &res.Value());
	if (!res.Value().Ok()) {
		report.permissionDenied =
			report.permissionDenied || IsPermissionError(res.Value().output.View());
		report.error = res.Value().output.Trim();
		if (report.error.IsEmpty())
			report.error = String::Format("code de sortie %d", res.Value().exitCode);
		return false;
	}
	return true;
}

[[nodiscard]] Result<CommandResult, String> RunChecked(const std::vector<String>& argv,
													   const CommandRunner& run) {
	auto res = run(argv);
	if (!res)
		return Err(res.Error());
	if (!res.Value().Ok()) {
		String msg = res.Value().output.Trim();
		return Err(msg.IsEmpty() ? String::Format("%s : code de sortie %d", argv[0].CStr(),
												  res.Value().exitCode)
								 : msg);
	}
	return Ok(std::move(res.Value()));
}

} // namespace

// ── Processus ───────────────────────────────────────────────────────────────

Result<CommandResult, String> RunCommand(const std::vector<String>& argv) {
	if (argv.empty())
		return Err(String("commande vide"));
	std::vector<const char*> args;
	args.reserve(argv.size() + 1);
	for (auto& a : argv)
		args.push_back(a.CStr());
	args.push_back(nullptr);

	// Sortie non traduite : les valeurs (Infra, connected…) restent analysables.
	SDL_Environment* env = SDL_CreateEnvironment(true);
	if (env) {
		SDL_SetEnvironmentVariable(env, "LC_ALL", "C", true);
		SDL_SetEnvironmentVariable(env, "LANG", "C", true);
	}
	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
	if (env)
		SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
	SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
	auto proc = sdl3::Process::CreateWithProperties(props);
	SDL_DestroyProperties(props);
	if (env)
		SDL_DestroyEnvironment(env);
	if (!proc)
		return Err(String::Format("%s : %s", argv[0].CStr(), String(proc.Error()).CStr()));

	auto out = proc.Value().Read();
	if (!out)
		return Err(String::Format("%s : %s", argv[0].CStr(), String(out.Error()).CStr()));
	CommandResult result;
	result.exitCode = out.Value().exitCode;
	result.output =
		String(reinterpret_cast<const char*>(out.Value().data.data()), out.Value().data.size());
	return Ok(std::move(result));
}

// ── Types ───────────────────────────────────────────────────────────────────

const char* WifiModeName(WifiMode mode) noexcept {
	switch (mode) {
	case WifiMode::INFRA:
		return "Infra";
	case WifiMode::ADHOC:
		return "Ad hoc";
	case WifiMode::MESH:
		return "Mesh";
	default:
		return "?";
	}
}

String AdhocConfig::ProfileName() const {
	return connectionName.IsEmpty() ? "adhoc-" + ssid : connectionName;
}

Option<String> AdhocConfig::Validate() const {
	if (ssid.IsEmpty())
		return Some(String("SSID vide"));
	if (ssid.size() > 32)
		return Some(String("SSID trop long (32 octets au plus)"));
	if (channel < 1 || channel > 14)
		return Some(String("canal 2,4 GHz attendu (1 à 14)"));
	if (!password.IsEmpty() && (password.size() < 8 || password.size() > 63))
		return Some(String("mot de passe WPA2 : 8 à 63 caractères"));
	if (ipv4 == Ipv4Method::MANUAL) {
		auto slash = address.Split('/');
		if (slash.size() != 2 || slash[0].Split('.').size() != 4 || slash[1].ToInt32() < 1 ||
			slash[1].ToInt32() > 30)
			return Some(
				String("adresse manuelle attendue sous la forme a.b.c.d/m (ex. 10.42.0.2/24)"));
		for (auto& part : slash[0].Split('.'))
			if (part.IsEmpty() || !part.IsNumeric() || part.ToInt32() > 255)
				return Some(String("adresse IPv4 invalide : ") + slash[0]);
	}
	return NONE;
}

// ── Analyse ─────────────────────────────────────────────────────────────────

std::vector<String> SplitTerseLine(StringView line) {
	std::vector<String> fields(1);
	for (size_t i = 0; i < line.GetSize(); ++i) {
		char c = line.GetData()[i];
		if (c == '\\' && i + 1 < line.GetSize()) {
			fields.back().Append(line.GetData()[++i]);
		} else if (c == ':') {
			fields.emplace_back();
		} else if (c != '\r') {
			fields.back().Append(c);
		}
	}
	return fields;
}

std::vector<WifiDevice> ParseDevices(StringView output) {
	std::vector<WifiDevice> out;
	for (auto& line : String(output).Lines()) {
		auto f = SplitTerseLine(line.View());
		if (f.size() < 4 || f[1] != "wifi")
			continue;
		out.push_back({f[0], f[2], f[3]});
	}
	return out;
}

std::vector<WifiNetwork> ParseNetworks(StringView output) {
	std::vector<WifiNetwork> out;
	for (auto& line : String(output).Lines()) {
		auto f = SplitTerseLine(line.View());
		if (f.size() < 9)
			continue;
		WifiNetwork n;
		n.inUse = f[0].Trim() == "*";
		n.bssid = f[1];
		n.ssid = f[2];
		n.mode = ParseMode(f[3]);
		n.channel = ToInt(f[4]);
		n.frequencyMHz = LeadingNumber(f[5]);
		n.rate = f[6];
		n.signal = ToInt(f[7]);
		n.security = f[8] == "--" ? String() : f[8].Trim();
		if (n.frequencyMHz == 0 && n.channel > 0)
			n.frequencyMHz = ChannelToFrequencyMHz(n.channel);
		out.push_back(std::move(n));
	}
	std::stable_sort(out.begin(), out.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
		if (a.IsAdhoc() != b.IsAdhoc())
			return a.IsAdhoc();
		return a.signal > b.signal;
	});
	return out;
}

WifiCapabilities ParseCapabilities(StringView output) {
	WifiCapabilities caps;
	for (auto& line : String(output).Lines()) {
		auto f = SplitTerseLine(line.View());
		if (f.size() < 2)
			continue;
		bool yes = f[1].Trim().ToLower() == "yes";
		if (f[0] == "WIFI-PROPERTIES.ADHOC")
			caps.adhoc = yes;
		else if (f[0] == "WIFI-PROPERTIES.IBSS-RSN")
			caps.ibssRsn = yes;
		else if (f[0] == "WIFI-PROPERTIES.2GHZ")
			caps.band2GHz = yes;
		else if (f[0] == "WIFI-PROPERTIES.5GHZ")
			caps.band5GHz = yes;
	}
	return caps;
}

std::vector<String> ParseIpv4Addresses(StringView output) {
	std::vector<String> out;
	for (auto& line : String(output).Lines()) {
		auto f = SplitTerseLine(line.View());
		if (f.size() >= 2 && f[0].StartsWith("IP4.ADDRESS") && !f[1].Trim().IsEmpty())
			out.push_back(f[1].Trim());
	}
	return out;
}

int ChannelToFrequencyMHz(int channel) noexcept {
	if (channel == 14)
		return 2484;
	if (channel >= 1 && channel <= 13)
		return 2407 + 5 * channel;
	if (channel >= 32 && channel <= 177)
		return 5000 + 5 * channel;
	return 0;
}

bool IsPermissionError(StringView output) {
	String s = Lower(output);
	return s.Contains("not authorized") || s.Contains("not authorised") ||
		   s.Contains("insufficient privileges") || s.Contains("permission denied") ||
		   s.Contains("operation not permitted") || s.Contains("polkit");
}

// ── Commandes ───────────────────────────────────────────────────────────────

std::vector<std::vector<String>> AdhocCommands(const AdhocConfig& config) {
	std::vector<String> add = {"nmcli", "connection", "add", "type", "wifi"};
	if (!config.device.IsEmpty()) {
		add.push_back("ifname");
		add.push_back(config.device);
	}
	add.push_back("con-name");
	add.push_back(config.ProfileName());
	add.insert(add.end(), {"ssid", config.ssid, "mode", "adhoc", "wifi.band", "bg", "wifi.channel",
						   String::Format("%d", config.channel), "connection.autoconnect", "no"});
	if (config.ipv4 == Ipv4Method::MANUAL)
		add.insert(add.end(), {"ipv4.method", "manual", "ipv4.addresses", config.address});
	else
		add.insert(add.end(), {"ipv4.method", "link-local"});
	add.insert(add.end(), {"ipv6.method", "link-local"});
	if (!config.password.IsEmpty())
		add.insert(add.end(),
				   {"wifi-sec.key-mgmt", "wpa-psk", "wifi-sec.proto", "rsn", "wifi-sec.pairwise",
					"ccmp", "wifi-sec.group", "ccmp", "wifi-sec.psk", config.password});

	std::vector<String> up = {
		"nmcli", "--wait", "30", "connection", "up", "id", config.ProfileName()};
	return {add, up};
}

String CommandLine(const std::vector<String>& argv, bool hideSecrets) {
	String line;
	for (size_t i = 0; i < argv.size(); ++i) {
		if (i)
			line.Append(' ');
		bool secret = hideSecrets && i > 0 && argv[i - 1] == "wifi-sec.psk";
		line.Append(secret ? String("********") : Quote(argv[i]));
	}
	return line;
}

String ManualInstructions(const AdhocConfig& config) {
	String dev = config.device.IsEmpty() ? String("wlan0") : config.device;
	String text = "Avec NetworkManager, en administrateur :\n";
	AdhocConfig withDevice = config;
	withDevice.device = dev;
	for (auto& cmd : AdhocCommands(withDevice))
		text.Append("  sudo " + CommandLine(cmd) + "\n");
	text.Append("\nSans NetworkManager (iw + ip, cellule ouverte) :\n");
	text.Append("  sudo nmcli device set " + Quote(dev) +
				" managed no   # si NetworkManager tourne\n");
	text.Append("  sudo ip link set " + Quote(dev) + " down\n");
	text.Append("  sudo iw dev " + Quote(dev) + " set type ibss\n");
	text.Append("  sudo ip link set " + Quote(dev) + " up\n");
	text.Append(String::Format("  sudo iw dev %s ibss join %s %d\n", Quote(dev).CStr(),
							   Quote(config.ssid).CStr(), ChannelToFrequencyMHz(config.channel)));
	String addr = config.ipv4 == Ipv4Method::MANUAL ? config.address : String("169.254.X.Y/16");
	text.Append("  sudo ip addr add " + addr + " dev " + Quote(dev) + "\n");
	text.Append("\nChaque ordinateur doit utiliser le même SSID et le même canal,\n"
				"et une adresse IPv4 différente dans le même sous-réseau.");
	return text;
}

// ── Exécution ───────────────────────────────────────────────────────────────

bool IsAvailable(const CommandRunner& run) {
	auto res = run({"nmcli", "-t", "-f", "RUNNING", "general"});
	return res && res.Value().Ok() && res.Value().output.Trim() == "running";
}

Result<std::vector<WifiDevice>, String> ListDevices(const CommandRunner& run) {
	auto res = RunChecked(
		{"nmcli", "-t", "-e", "yes", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device", "status"},
		run);
	if (!res)
		return Err(res.Error());
	return Ok(ParseDevices(res.Value().output.View()));
}

Result<WifiCapabilities, String> Capabilities(const String& device, const CommandRunner& run) {
	auto res = RunChecked({"nmcli", "-t", "-f", "WIFI-PROPERTIES", "device", "show", device}, run);
	if (!res)
		return Err(res.Error());
	return Ok(ParseCapabilities(res.Value().output.View()));
}

Result<std::vector<String>, String> Ipv4Addresses(const String& device, const CommandRunner& run) {
	auto res = RunChecked({"nmcli", "-t", "-f", "IP4.ADDRESS", "device", "show", device}, run);
	if (!res)
		return Err(res.Error());
	return Ok(ParseIpv4Addresses(res.Value().output.View()));
}

Result<std::vector<WifiNetwork>, String> Scan(const String& device, bool rescan,
											  const CommandRunner& run) {
	std::vector<String> argv = {"nmcli",
								"-t",
								"-e",
								"yes",
								"-f",
								"IN-USE,BSSID,SSID,MODE,CHAN,FREQ,RATE,SIGNAL,SECURITY",
								"device",
								"wifi",
								"list",
								"--rescan",
								rescan ? "yes" : "no"};
	if (!device.IsEmpty()) {
		argv.push_back("ifname");
		argv.push_back(device);
	}
	auto res = RunChecked(argv, run);
	if (!res)
		return Err(res.Error());
	return Ok(ParseNetworks(res.Value().output.View()));
}

ConfigureReport JoinAdhoc(const AdhocConfig& config, const CommandRunner& run) {
	ConfigureReport report;
	if (auto problem = config.Validate()) {
		report.error = *problem;
		return report;
	}
	// Un profil du même nom (essai précédent) : le remplacer. Son absence
	// n'est pas une erreur.
	std::vector<String> del = {"nmcli", "connection", "delete", "id", config.ProfileName()};
	if (auto res = run(del); res && res.Value().Ok())
		Log(report, del, &res.Value());
	for (auto& cmd : AdhocCommands(config))
		if (!Step(report, cmd, run))
			return report;
	report.ok = true;
	return report;
}

Option<FirewallInfo> DetectFirewall(uint16_t udpPort, uint16_t tcpFirst, uint16_t tcpLast,
									const CommandRunner& run) {
	auto active = [&](const char* unit) {
		auto res = run({"systemctl", "is-active", unit});
		return res && res.Value().output.Trim() == "active";
	};
	if (active("ufw"))
		return Some(FirewallInfo{
			"ufw",
			{String::Format("sudo ufw allow %u/udp", unsigned(udpPort)),
			 String::Format("sudo ufw allow %u:%u/tcp", unsigned(tcpFirst), unsigned(tcpLast))}});
	if (active("firewalld"))
		return Some(FirewallInfo{
			"firewalld",
			{String::Format("sudo firewall-cmd --add-port=%u/udp --add-port=%u-%u/tcp",
							unsigned(udpPort), unsigned(tcpFirst), unsigned(tcpLast))}});
	return NONE;
}

ConfigureReport LeaveAdhoc(const AdhocConfig& config, const CommandRunner& run) {
	ConfigureReport report;
	std::vector<String> down = {"nmcli", "connection", "down", "id", config.ProfileName()};
	auto res = run(down);
	if (res)
		Log(report, down, &res.Value());
	if (!Step(report, {"nmcli", "connection", "delete", "id", config.ProfileName()}, run))
		return report;
	// Rendre l'interface à NetworkManager : il y remonte la connexion habituelle.
	if (!config.device.IsEmpty()) {
		std::vector<String> reconnect = {"nmcli", "device", "connect", config.device};
		if (auto back = run(reconnect))
			Log(report, reconnect, &back.Value());
	}
	report.ok = true;
	return report;
}

} // namespace net::wifi
