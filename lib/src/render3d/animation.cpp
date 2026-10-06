// Définitions de render3d/animation.hpp
#include "render3d/animation.hpp"

namespace render3d {

// ── AnimationClip ────────────────────────────────────────────────────────────

float AnimationClip::Duration() const noexcept {
	float duration = 0.f;
	auto scan = [&](const auto &tracks) {
		for (const auto &track : tracks)
			if (!track.keyframes.empty())
				duration = std::max(duration, track.keyframes.back().time);
	};
	scan(positionTracks);
	scan(rotationTracks);
	scan(scaleTracks);
	return duration;
}

// ── AnimationMixer ───────────────────────────────────────────────────────────

AnimationMixer::ClipHandle AnimationMixer::Play(const AnimationClip &clip, bool loop) {
	m_active.push_back(ActiveClip{&clip, 0.f, loop});
	return m_active.size() - 1;
}

void AnimationMixer::Stop(ClipHandle handle) noexcept {
	if (handle < m_active.size())
		m_active[handle].clip = nullptr;
}

void AnimationMixer::Update(float dt) {
	for (ActiveClip &active : m_active) {
		if (!active.clip)
			continue;

		float duration = active.clip->Duration();
		active.time += dt;
		if (duration > 1e-8f)
			active.time = active.loop ? std::fmod(active.time, duration) : std::min(active.time, duration);
		else
			active.time = 0.f;

		ApplyTracks(active.clip->positionTracks, active.time,
					[](Object3D &node, const math::FVector3 &v) { node.SetPosition(v); });
		ApplyTracks(active.clip->rotationTracks, active.time,
					[](Object3D &node, const math::FQuaternion &v) { node.SetRotation(v); });
		ApplyTracks(active.clip->scaleTracks, active.time,
					[](Object3D &node, const math::FVector3 &v) { node.SetScale(v); });
	}
}

} // namespace render3d
