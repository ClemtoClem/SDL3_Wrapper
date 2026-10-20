// Définitions de settings.hpp — préférences du joueur.
#include <voxelgame/settings.hpp>

#include <algorithm>

#include <SDL3/SDL.h>
#include <voxel/json.hpp>

namespace voxelgame {

namespace json = voxel::json;
using dataio::NodePtr;

String InputName(int code) {
	if (code <= 0)
		return String("—");
	if (code == WHEEL_UP)
		return String("Molette ↑");
	if (code == WHEEL_DOWN)
		return String("Molette ↓");
	if (code >= MOUSE_BASE) {
		switch (code - MOUSE_BASE) {
		case 1: return String("Clic gauche");
		case 2: return String("Clic milieu");
		case 3: return String("Clic droit");
		default: return String::Format("Souris %d", code - MOUSE_BASE);
		}
	}
	// Nom de la touche dans la disposition du clavier (AZERTY : « Z »).
	const SDL_Keycode key = SDL_GetKeyFromScancode(SDL_Scancode(code), SDL_KMOD_NONE, false);
	const char *name = SDL_GetKeyName(key);
	if (name && *name)
		return String(name);
	return String(SDL_GetScancodeName(SDL_Scancode(code)));
}

void Settings::ResetBindings() {
	auto key = [](SDL_Scancode s) { return int(s); };
	const int mouseLeft = MOUSE_BASE + 1, mouseMiddle = MOUSE_BASE + 2, mouseRight = MOUSE_BASE + 3;
	bindings = {
		{"forward", "Avancer", "Déplacement", {key(SDL_SCANCODE_W), key(SDL_SCANCODE_UP)}, {}},
		{"back", "Reculer", "Déplacement", {key(SDL_SCANCODE_S), key(SDL_SCANCODE_DOWN)}, {}},
		{"left", "Aller à gauche", "Déplacement", {key(SDL_SCANCODE_A), key(SDL_SCANCODE_LEFT)}, {}},
		{"right", "Aller à droite", "Déplacement", {key(SDL_SCANCODE_D), key(SDL_SCANCODE_RIGHT)}, {}},
		{"jump", "Sauter / monter", "Déplacement", {key(SDL_SCANCODE_SPACE), 0}, {}},
		{"sneak", "S'accroupir / descendre", "Déplacement", {key(SDL_SCANCODE_LSHIFT), 0}, {}},
		{"sprint", "Courir", "Déplacement", {key(SDL_SCANCODE_LCTRL), 0}, {}},
		{"fly", "Voler (créatif)", "Déplacement", {key(SDL_SCANCODE_F), 0}, {}},
		{"attack", "Casser / frapper", "Actions", {mouseLeft, 0}, {}},
		{"use", "Poser / utiliser", "Actions", {mouseRight, 0}, {}},
		{"pick", "Choisir le bloc visé", "Actions", {mouseMiddle, 0}, {}},
		{"drop", "Jeter l'objet", "Actions", {key(SDL_SCANCODE_Q), 0}, {}},
		{"nextSlot", "Objet suivant", "Actions", {WHEEL_DOWN, 0}, {}},
		{"prevSlot", "Objet précédent", "Actions", {WHEEL_UP, 0}, {}},
		{"inventory", "Inventaire", "Interface", {key(SDL_SCANCODE_E), key(SDL_SCANCODE_I)}, {}},
		{"crafting", "Fabrication", "Interface", {key(SDL_SCANCODE_C), 0}, {}},
		{"chat", "Discussion", "Interface", {key(SDL_SCANCODE_T), key(SDL_SCANCODE_RETURN)}, {}},
		{"command", "Terminal de commandes", "Interface", {key(SDL_SCANCODE_SLASH), key(SDL_SCANCODE_GRAVE)}, {}},
		{"worldInfo", "Informations du monde", "Interface", {key(SDL_SCANCODE_TAB), 0}, {}},
		{"pause", "Pause / menu", "Interface", {key(SDL_SCANCODE_ESCAPE), key(SDL_SCANCODE_P)}, {}},
		{"hideHud", "Masquer l'interface", "Interface", {key(SDL_SCANCODE_F1), 0}, {}},
		{"screenshot", "Capture d'écran", "Interface", {key(SDL_SCANCODE_F2), 0}, {}},
		{"debug", "Informations techniques", "Interface", {key(SDL_SCANCODE_F3), 0}, {}},
		{"camera", "Vue (1re / 3e personne)", "Interface", {key(SDL_SCANCODE_F5), 0}, {}},
		{"save", "Enregistrer le monde", "Interface", {key(SDL_SCANCODE_F6), 0}, {}},
		{"fullscreen", "Plein écran", "Interface", {key(SDL_SCANCODE_F11), 0}, {}},
	};
	for (int i = 1; i <= 9; ++i)
		bindings.push_back({String::Format("slot%d", i), String::Format("Emplacement %d", i), "Barre d'objets",
							{key(SDL_Scancode(SDL_SCANCODE_1 + i - 1)), 0}, {}});
	for (KeyBinding &b : bindings)
		b.defaults = b.keys;
}

const KeyBinding *Settings::Binding(const String &action) const {
	for (const KeyBinding &b : bindings)
		if (b.action == action)
			return &b;
	return nullptr;
}

bool Settings::Matches(const String &action, int code) const {
	const KeyBinding *b = Binding(action);
	return b && code != 0 && (b->keys[0] == code || b->keys[1] == code);
}

std::vector<std::pair<String, String>> Settings::Conflicts() const {
	std::vector<std::pair<String, String>> out;
	for (size_t i = 0; i < bindings.size(); ++i)
		for (size_t j = i + 1; j < bindings.size(); ++j)
			for (int a : bindings[i].keys)
				for (int b : bindings[j].keys)
					if (a != 0 && a == b)
						out.push_back({bindings[i].action, bindings[j].action});
	return out;
}

namespace {

/// Table des champs simples : une seule liste sert à l'écriture et à la lecture.
template <typename S, typename F> void VisitFields(S &s, F &&f) {
	f("language", s.language);
	f("playerName", s.playerName);
	f("skin", s.skin);
	f("confirmQuit", s.confirmQuit);
	f("confirmDelete", s.confirmDelete);
	f("confirmModChanges", s.confirmModChanges);
	f("notifications", s.notifications);
	f("notificationSeconds", s.notificationSeconds);
	f("autosaveMinutes", s.autosaveMinutes);
	f("showTips", s.showTips);
	f("pauseOnFocusLoss", s.pauseOnFocusLoss);
	f("mouseSensitivity", s.mouseSensitivity);
	f("invertY", s.invertY);
	f("fov", s.fov);
	f("viewBobbing", s.viewBobbing);
	f("allowDualBinding", s.allowDualBinding);
	f("toggleSprint", s.toggleSprint);
	f("toggleSneak", s.toggleSneak);
	f("thirdPersonSmooth", s.thirdPersonSmooth);
	f("displayMode", s.displayMode);
	f("width", s.width);
	f("height", s.height);
	f("vsync", s.vsync);
	f("fpsLimit", s.fpsLimit);
	f("viewDistance", s.viewDistance);
	f("chunkUpdates", s.chunkUpdates);
	f("shadows", s.shadows);
	f("dynamicLights", s.dynamicLights);
	f("brightness", s.brightness);
	f("ambientOcclusion", s.ambientOcclusion);
	f("clouds", s.clouds);
	f("vignette", s.vignette);
	f("screenEffects", s.screenEffects);
	f("particles", s.particles);
	f("vegetationDensity", s.vegetationDensity);
	f("entityDistance", s.entityDistance);
	f("masterVolume", s.masterVolume);
	f("musicVolume", s.musicVolume);
	f("effectsVolume", s.effectsVolume);
	f("ambienceVolume", s.ambienceVolume);
	f("uiVolume", s.uiVolume);
	f("voiceVolume", s.voiceVolume);
	f("spatialAudio", s.spatialAudio);
	f("muteWhenUnfocused", s.muteWhenUnfocused);
	f("showCoordinates", s.showCoordinates);
	f("showFps", s.showFps);
	f("showBiome", s.showBiome);
	f("showClock", s.showClock);
	f("crosshair", s.crosshair);
	f("hudOpacity", s.hudOpacity);
	f("hudScale", s.hudScale);
	f("autoJump", s.autoJump);
	f("placeAssist", s.placeAssist);
	f("showHints", s.showHints);
	f("chatTimestamps", s.chatTimestamps);
	f("chatOpacity", s.chatOpacity);
	f("uiScale", s.uiScale);
	f("textScale", s.textScale);
	f("highContrast", s.highContrast);
	f("colorBlind", s.colorBlind);
	f("subtitles", s.subtitles);
	f("reduceMotion", s.reduceMotion);
	f("reduceFlashes", s.reduceFlashes);
	f("hudBackground", s.hudBackground);
	f("crosshairColor", s.crosshairColor);
	f("crosshairSize", s.crosshairSize);
	f("smoothTextures", s.smoothTextures);
}

} // namespace

NodePtr Settings::ToJson() const {
	NodePtr root = dataio::Node::MakeObject();
	VisitFields(*this, [&](const char *name, const auto &value) {
		using T = std::decay_t<decltype(value)>;
		if constexpr (std::is_same_v<T, bool>)
			root->Set(name, json::MakeBool(value));
		else if constexpr (std::is_same_v<T, int>)
			root->Set(name, json::MakeInt(value));
		else if constexpr (std::is_same_v<T, float>)
			root->Set(name, json::MakeNum(value));
		else
			root->Set(name, json::MakeStr(value));
	});
	NodePtr keys = dataio::Node::MakeObject();
	for (const KeyBinding &b : bindings) {
		NodePtr pair = dataio::Node::MakeArray();
		pair->Push(json::MakeInt(b.keys[0]));
		pair->Push(json::MakeInt(b.keys[1]));
		keys->Set(b.action, pair);
	}
	root->Set("bindings", keys);
	NodePtr order = dataio::Node::MakeArray();
	for (const String &id : modOrder)
		order->Push(json::MakeStr(id));
	root->Set("modOrder", order);
	NodePtr names = dataio::Node::MakeObject();
	for (const auto &[id, name] : modNames)
		names->Set(String(id.c_str()), json::MakeStr(name));
	root->Set("modNames", names);
	NodePtr cats = dataio::Node::MakeObject();
	for (const auto &[id, cat] : modCategories)
		cats->Set(String(id.c_str()), json::MakeStr(cat));
	root->Set("modCategories", cats);
	if (modSettings)
		root->Set("modSettings", modSettings->Clone());
	return root;
}

void Settings::FromJson(const NodePtr &node) {
	ResetBindings();
	if (!node)
		return;
	VisitFields(*this, [&](const char *name, auto &value) {
		using T = std::decay_t<decltype(value)>;
		if (!json::Has(node, name))
			return;
		if constexpr (std::is_same_v<T, bool>)
			value = json::Bool(node, name, value);
		else if constexpr (std::is_same_v<T, int>)
			value = json::Int(node, name, value);
		else if constexpr (std::is_same_v<T, float>)
			value = float(json::Num(node, name, double(value)));
		else
			value = json::Str(node, name, value);
	});
	if (NodePtr keys = json::At(node, "bindings"); keys && keys->IsObject())
		for (KeyBinding &b : bindings)
			if (NodePtr pair = keys->Get(b.action); pair && pair->IsArray() && pair->GetSize() >= 2) {
				b.keys[0] = json::Int(pair->At(0), "", b.keys[0]);
				b.keys[1] = json::Int(pair->At(1), "", b.keys[1]);
			}
	modOrder = json::StrList(node, "modOrder");
	modNames.clear();
	modCategories.clear();
	if (NodePtr names = json::At(node, "modNames"); names && names->IsObject())
		for (const String &k : names->Keys())
			modNames[k.CStr()] = json::Str(names, k.CStr());
	if (NodePtr cats = json::At(node, "modCategories"); cats && cats->IsObject())
		for (const String &k : cats->Keys())
			modCategories[k.CStr()] = json::Str(cats, k.CStr());
	modSettings = json::At(node, "modSettings");
	viewDistance = std::clamp(viewDistance, 2, 16);
	uiScale = std::clamp(uiScale, 0.75f, 1.5f);
	textScale = std::clamp(textScale, 0.8f, 1.6f);
}

String Settings::ModName(const String &id, const String &fallback) const {
	auto it = modNames.find(id.CStr());
	return it != modNames.end() && !it->second.IsEmpty() ? it->second : fallback;
}

Result<bool, String> Settings::Save(const String &path) const { return json::WriteFile(path, ToJson()); }

Settings Settings::Load(const String &path) {
	Settings s;
	s.ResetBindings();
	if (Result<NodePtr, String> read = json::ReadFile(path); read.IsOk())
		s.FromJson(read.Unwrap());
	return s;
}

} // namespace voxelgame
