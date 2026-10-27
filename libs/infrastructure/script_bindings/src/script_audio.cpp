// Définitions de script_bindings/script_audio.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include <script/script.hpp>
#include <script_bindings/script_audio.hpp>

#include <algorithm>

namespace script {

namespace {

using lib::Args;
using lib::Fail;
using audio::AudioEngine;

/// Options d'une table : lecture typée, et refus des clés inconnues.
class AudioOptions {
public:
	AudioOptions(const Args &args, size_t index, const char *fn) : m_fn(fn) {
		if (index >= args.size() || args[index].IsNil())
			return;
		if (args[index].IsMap() && args[index].AsMap())
			m_entries = args[index].AsMap()->Snapshot();
		else
			Fault(String::Format("`%s` : table d'options attendue, trouvé `%s`", fn, args[index].TypeName()));
	}
	[[nodiscard]] const Value *Get(const char *key) {
		m_known.push_back(String(key));
		for (const auto &entry : m_entries)
			if (entry.first == key)
				return &entry.second;
		return nullptr;
	}
	[[nodiscard]] Option<float> Number(const char *key) {
		const Value *v = Get(key);
		if (!v)
			return NONE;
		if (!v->IsNumber()) {
			Fault(String::Format("`%s` : option `%s` : nombre attendu", m_fn, key));
			return NONE;
		}
		return Some(float(v->AsNumber()));
	}
	[[nodiscard]] float Number(const char *key, float fallback) {
		const Option<float> v = Number(key);
		return v.IsSome() ? v.Value() : fallback;
	}
	[[nodiscard]] bool Bool(const char *key, bool fallback) {
		const Value *v = Get(key);
		return v ? v->IsTruthy() : fallback;
	}
	/// Position [x, y, z] ; NONE si absente.
	[[nodiscard]] Option<math::FVector3> Vec3(const char *key) {
		const Value *v = Get(key);
		if (!v)
			return NONE;
		if (v->IsList() && v->AsList() && v->AsList()->items.size() == 3) {
			const auto &items = v->AsList()->items;
			if (std::all_of(items.begin(), items.end(), [](const Value &x) { return x.IsNumber(); }))
				return Some(math::FVector3{float(items[0].AsNumber()), float(items[1].AsNumber()), float(items[2].AsNumber())});
		}
		Fault(String::Format("`%s` : option `%s` : liste [x, y, z] attendue", m_fn, key));
		return NONE;
	}
	void Fault(String message) {
		if (m_error.IsNone())
			m_error = Some(Fail(std::move(message)));
	}
	/// Erreur de lecture, ou clé inconnue (après toutes les lectures).
	[[nodiscard]] Option<ScriptError> Finish() const {
		if (m_error.IsSome())
			return m_error;
		for (const auto &entry : m_entries)
			if (std::find(m_known.begin(), m_known.end(), entry.first) == m_known.end())
				return Some(Fail(String::Format("`%s` : option `%s` inconnue", m_fn, entry.first.CStr())));
		return NONE;
	}

private:
	const char *m_fn;
	std::vector<std::pair<String, Value>> m_entries;
	std::vector<String> m_known;
	Option<ScriptError> m_error = NONE;
};

Result<AudioEngine::VoiceId, ScriptError> VoiceArg(const Args &args, size_t i, const char *fn) {
	auto id = lib::ArgFloat(args, i, fn);
	if (id.IsError())
		return Err(id.Error());
	return Ok(AudioEngine::VoiceId(std::max(id.Value(), 0.0)));
}

/// Options de lecture communes (`audio.play`, `audio.play_on`).
audio::AudioPlayOptions ReadPlay(AudioOptions &o) {
	audio::AudioPlayOptions options;
	options.volume = o.Number("volume", 1.f);
	options.loop = o.Bool("loop", false);
	options.pitch = o.Number("pitch", 1.f);
	if (Option<math::FVector3> position = o.Vec3("position"); position.IsSome()) {
		options.spatial = true;
		options.position = position.Value();
	}
	options.spatial = o.Bool("spatial", options.spatial);
	return options;
}

Value FadeMap(const audio::ChannelFade &fade) {
	auto map = std::make_shared<MapObject>();
	map->SetKey(String("fade_in"), Value::Number(double(fade.fadeIn)));
	map->SetKey(String("fade_out"), Value::Number(double(fade.fadeOut)));
	map->SetKey(String("sequential"), Value::Boolean(fade.sequential));
	return Value::Map(std::move(map));
}

} // namespace

void InstallAudioLibrary(Interpreter &vm, AudioEngine &engine, AudioPathResolver resolve) {
	const String ns("audio");
	if (!resolve)
		resolve = [](const String &path) { return path; };
	AudioEngine *e = &engine;

	// ── Voix ─────────────────────────────────────────────────────────────────
	vm.RegisterNamespacedNative(ns, String("play"), 1, 2, [e, resolve](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		auto clip = lib::ArgText(args, 0, "audio.play");
		if (clip.IsError())
			return Err(clip.Error());
		AudioOptions o(args, 1, "audio.play");
		const audio::AudioPlayOptions options = ReadPlay(o);
		const float fadeIn = o.Number("fade_in", 0.f);
		if (auto error = o.Finish(); error.IsSome())
			return Err(error.Unwrap());
		const AudioEngine::VoiceId voice = e->Play(resolve(clip.Value()), options);
		if (voice != AudioEngine::NO_VOICE && fadeIn > 0.f) {
			(void)e->FadeTo(voice, 0.f, 0.f); // part du silence…
			(void)e->FadeTo(voice, options.volume, fadeIn); // …et monte
		}
		return Ok(voice == AudioEngine::NO_VOICE ? Value::Nil() : Value::Number(double(voice)));
	});
	vm.RegisterNamespacedNative(ns, String("stop"), 1, 2, [e](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		auto voice = VoiceArg(args, 0, "audio.stop");
		if (voice.IsError())
			return Err(voice.Error());
		float fadeOut = 0.f;
		if (args.size() > 1) {
			auto f = lib::ArgFloat(args, 1, "audio.stop");
			if (f.IsError())
				return Err(f.Error());
			fadeOut = float(f.Value());
		}
		return Ok(Value::Boolean(e->Stop(voice.Value(), fadeOut)));
	});
	vm.RegisterNamespacedNative(ns, String("stop_all"), 0, 0, [e](Interpreter &, Args &) -> Result<Value, ScriptError> {
		e->StopAll();
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative(ns, String("playing"), 1, 1, [e](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		auto voice = VoiceArg(args, 0, "audio.playing");
		if (voice.IsError())
			return Err(voice.Error());
		return Ok(Value::Boolean(e->IsPlaying(voice.Value())));
	});
	// audio.volume(voix) : volume entendu ; audio.volume(voix, v) : le change.
	vm.RegisterNamespacedNative(ns, String("volume"), 1, 2, [e](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		auto voice = VoiceArg(args, 0, "audio.volume");
		if (voice.IsError())
			return Err(voice.Error());
		if (args.size() > 1) {
			auto volume = lib::ArgFloat(args, 1, "audio.volume");
			if (volume.IsError())
				return Err(volume.Error());
			(void)e->SetVolume(voice.Value(), float(volume.Value()));
		}
		const float heard = e->EffectiveVolume(voice.Value());
		return Ok(heard < 0.f ? Value::Nil() : Value::Number(double(heard)));
	});
	// audio.fade(voix, volume, secondes[, arrêter_à_la_fin]).
	vm.RegisterNamespacedNative(ns, String("fade"), 3, 4, [e](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		auto voice = VoiceArg(args, 0, "audio.fade");
		if (voice.IsError())
			return Err(voice.Error());
		auto volume = lib::ArgFloat(args, 1, "audio.fade");
		auto seconds = lib::ArgFloat(args, 2, "audio.fade");
		if (volume.IsError())
			return Err(volume.Error());
		if (seconds.IsError())
			return Err(seconds.Error());
		const bool stop = args.size() > 3 && args[3].IsTruthy();
		return Ok(Value::Boolean(e->FadeTo(voice.Value(), float(volume.Value()), float(seconds.Value()), stop)));
	});
	vm.RegisterNamespacedNative(ns, String("pitch"), 2, 2, [e](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		auto voice = VoiceArg(args, 0, "audio.pitch");
		auto pitch = lib::ArgFloat(args, 1, "audio.pitch");
		if (voice.IsError())
			return Err(voice.Error());
		if (pitch.IsError())
			return Err(pitch.Error());
		return Ok(Value::Boolean(e->SetPitch(voice.Value(), float(pitch.Value()))));
	});
	vm.RegisterNamespacedNative(ns, String("set_position"), 4, 4, [e](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		auto voice = VoiceArg(args, 0, "audio.set_position");
		if (voice.IsError())
			return Err(voice.Error());
		float p[3];
		for (size_t i = 0; i < 3; ++i) {
			auto v = lib::ArgFloat(args, i + 1, "audio.set_position");
			if (v.IsError())
				return Err(v.Error());
			p[i] = float(v.Value());
		}
		return Ok(Value::Boolean(e->SetPosition(voice.Value(), math::FVector3{p[0], p[1], p[2]})));
	});
	vm.RegisterNamespacedNative(ns, String("master"), 0, 1, [e](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		if (!args.empty()) {
			auto volume = lib::ArgFloat(args, 0, "audio.master");
			if (volume.IsError())
				return Err(volume.Error());
			e->SetMasterVolume(float(volume.Value()));
		}
		return Ok(Value::Number(double(e->MasterVolume())));
	});

	// ── Canaux ───────────────────────────────────────────────────────────────
	vm.RegisterNamespacedNative(ns, String("play_on"), 2, 3, [e, resolve](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		auto channel = lib::ArgText(args, 0, "audio.play_on");
		if (channel.IsError())
			return Err(channel.Error());
		auto clip = lib::ArgText(args, 1, "audio.play_on");
		if (clip.IsError())
			return Err(clip.Error());
		AudioOptions o(args, 2, "audio.play_on");
		const audio::AudioPlayOptions options = ReadPlay(o);
		// Fondus : ceux du canal, remplacés clé par clé.
		audio::ChannelFade fade = e->ChannelFadeOf(channel.Value());
		fade.fadeIn = o.Number("fade_in", fade.fadeIn);
		fade.fadeOut = o.Number("fade_out", fade.fadeOut);
		fade.sequential = o.Bool("sequential", fade.sequential);
		fade.restart = o.Bool("restart", false);
		if (auto error = o.Finish(); error.IsSome())
			return Err(error.Unwrap());
		const AudioEngine::VoiceId voice = e->PlayOnChannel(channel.Value(), resolve(clip.Value()), options, Some(fade));
		return Ok(voice == AudioEngine::NO_VOICE ? Value::Nil() : Value::Number(double(voice)));
	});
	vm.RegisterNamespacedNative(ns, String("stop_channel"), 1, 2, [e](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		auto channel = lib::ArgText(args, 0, "audio.stop_channel");
		if (channel.IsError())
			return Err(channel.Error());
		Option<float> fadeOut = NONE;
		if (args.size() > 1) {
			auto f = lib::ArgFloat(args, 1, "audio.stop_channel");
			if (f.IsError())
				return Err(f.Error());
			fadeOut = Some(float(f.Value()));
		}
		return Ok(Value::Boolean(e->StopChannel(channel.Value(), fadeOut)));
	});
	vm.RegisterNamespacedNative(ns, String("channel"), 1, 1, [e](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		auto channel = lib::ArgText(args, 0, "audio.channel");
		if (channel.IsError())
			return Err(channel.Error());
		const AudioEngine::VoiceId voice = e->ChannelVoice(channel.Value());
		return Ok(voice == AudioEngine::NO_VOICE ? Value::Nil() : Value::Number(double(voice)));
	});
	// audio.channel_fade(canal[, {fade_in, fade_out, sequential}]) : règle les
	// fondus par défaut du canal ; rend ceux en vigueur.
	vm.RegisterNamespacedNative(ns, String("channel_fade"), 1, 2, [e](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		auto channel = lib::ArgText(args, 0, "audio.channel_fade");
		if (channel.IsError())
			return Err(channel.Error());
		audio::ChannelFade fade = e->ChannelFadeOf(channel.Value());
		if (args.size() > 1) {
			AudioOptions o(args, 1, "audio.channel_fade");
			fade.fadeIn = std::max(0.f, o.Number("fade_in", fade.fadeIn));
			fade.fadeOut = std::max(0.f, o.Number("fade_out", fade.fadeOut));
			fade.sequential = o.Bool("sequential", fade.sequential);
			if (auto error = o.Finish(); error.IsSome())
				return Err(error.Unwrap());
			e->SetChannelFade(channel.Value(), fade);
		}
		return Ok(FadeMap(fade));
	});
}

} // namespace script
