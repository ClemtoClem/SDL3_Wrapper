#pragma once
/**
 * @file animation.hpp
 * @brief Generic Object3D-level keyframe animation (three.js AnimationClip /
 * AnimationMixer, M27 of the plan).
 *
 * Because `Bone : public Object3D` (skinned_mesh.hpp), driving arbitrary
 * Object3D nodes' position/rotation/scale through named keyframe tracks gets
 * skeletal animation "for free" — no separate skeletal-only code path is
 * needed, AnimationMixer just happens to also work on Bone hierarchies.
 *
 * Correctness-by-recomputation, not caching: AnimationMixer::Update()
 * resolves each track's target name to an Object3D* via
 * Object3D::FindByName() FRESH every call — never cached across frames. This
 * matches this codebase's established policy (see Object3D::WorldMatrix()'s
 * own doc comment in object3d.hpp, and physics::RigidBody::WorldInverseInertia())
 * and avoids a dangling-pointer hazard if the hierarchy is rebuilt between
 * frames.
 */
#include "../core/core.hpp"
#include "../math/math.hpp"
#include "object3d.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <vector>

namespace render3d {

/// One (time, value) sample of a KeyframeTrack.
template <typename T> struct Keyframe {
	float time = 0.f;
	T value{};
};

/**
 * A sorted sequence of keyframes driving ONE property (position/scale as
 * math::FVector3, rotation as math::FQuaternion) of ONE Object3D, resolved
 * by name (see AnimationMixer). A single template covers both
 * instantiations — this repo's own precedent (Curve3 in curve.hpp) uses a
 * virtual-class-per-kind style, but with only two concrete kinds here
 * (FVector3/FQuaternion) a template is simpler and avoids the virtual-call
 * overhead of sampling every track every frame.
 */
template <typename T> struct KeyframeTrack {
	String targetName;
	std::vector<Keyframe<T>> keyframes; // must be sorted by `time` ascending

	/**
	 * Interpolated value at `time`:
	 *  - `time` <= first keyframe's time -> first keyframe's value
	 *  - `time` >= last keyframe's time  -> last keyframe's value
	 *  - otherwise, Lerp (FVector3) / Slerp (FQuaternion, shortest path
	 *    handled internally) between the two bracketing keyframes.
	 */
	[[nodiscard]] T Sample(float time) const noexcept {
		if (keyframes.empty())
			return T{};
		if (keyframes.size() == 1 || time <= keyframes.front().time)
			return keyframes.front().value;
		if (time >= keyframes.back().time)
			return keyframes.back().value;

		for (size_t i = 1; i < keyframes.size(); ++i) {
			if (time <= keyframes[i].time) {
				const Keyframe<T> &a = keyframes[i - 1];
				const Keyframe<T> &b = keyframes[i];
				float span = b.time - a.time;
				float t = span > 1e-8f ? (time - a.time) / span : 0.f;
				return Interpolate(a.value, b.value, t);
			}
		}
		return keyframes.back().value; // unreachable given the guards above
	}

private:
	[[nodiscard]] static T Interpolate(const T &from, const T &to, float t) noexcept {
		if constexpr (std::is_same_v<T, math::FQuaternion>)
			return from.Slerp(to, t);
		else
			return from.Lerp(to, t);
	}
};

/// A named collection of position/rotation/scale tracks, each targeting an
/// Object3D by name — one clip can drive many nodes at once (e.g. a full
/// skeleton's worth of bones).
struct AnimationClip {
	String name;
	std::vector<KeyframeTrack<math::FVector3>> positionTracks;
	std::vector<KeyframeTrack<math::FQuaternion>> rotationTracks;
	std::vector<KeyframeTrack<math::FVector3>> scaleTracks;

	/// Clip length: the latest keyframe time across every track (0 if the
	/// clip has no tracks, or every track is empty).
	[[nodiscard]] float Duration() const noexcept {
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
};

/**
 * Drives one or more AnimationClips against a scene root, each with its own
 * independent time cursor. Holds the root non-owning (raw pointer) — exactly
 * how `Bone *m_rootBone` is held in skinned_mesh.hpp: Object3D is
 * non-copyable/non-movable (see its class comment in object3d.hpp), so a
 * mixer can only reference an existing hierarchy, never own/copy one.
 *
 * Out of scope for this milestone (see the plan): no blending between two
 * simultaneously-playing clips that target the same track — last-applied-
 * wins (clips are applied in Play() order every Update()).
 */
class AnimationMixer {
	struct ActiveClip {
		const AnimationClip *clip; // nullptr = stopped, slot kept so handles stay valid
		float time = 0.f;
		bool loop = true;
	};

public:
	using ClipHandle = size_t;

	explicit AnimationMixer(Object3D &root) noexcept : m_root(&root) {}

	/// Starts playing `clip` from t=0 with its own independent cursor;
	/// returns a handle usable with Stop(). `clip` is referenced, not
	/// copied — it must outlive its use by the mixer.
	ClipHandle Play(const AnimationClip &clip, bool loop = true) {
		m_active.push_back(ActiveClip{&clip, 0.f, loop});
		return m_active.size() - 1;
	}

	/// Stops the clip at `handle` (no-op if already stopped/out of range).
	/// The slot is cleared, not erased, so previously returned handles stay
	/// valid across further Play()/Stop() calls.
	void Stop(ClipHandle handle) noexcept {
		if (handle < m_active.size())
			m_active[handle].clip = nullptr;
	}

	/**
	 * Advances every active clip's cursor by `dt`, samples every track at
	 * the new time, and applies the sampled value via the target node's
	 * SetPosition()/SetRotation()/SetScale(). Each track's `targetName` is
	 * resolved to an Object3D* via FindByName() fresh here (see file
	 * comment) — a name that doesn't resolve is skipped silently (this
	 * repo's best-effort, defensive style — no assert/throw).
	 */
	void Update(float dt) {
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

private:
	template <typename T, typename Apply>
	void ApplyTracks(const std::vector<KeyframeTrack<T>> &tracks, float time, Apply apply) {
		for (const KeyframeTrack<T> &track : tracks) {
			Object3D *target = m_root->FindByName(track.targetName.View());
			if (!target)
				continue; // best-effort: silently skip unresolved targets
			apply(*target, track.Sample(time));
		}
	}

	Object3D *m_root;
	std::vector<ActiveClip> m_active;
};

} // namespace render3d
