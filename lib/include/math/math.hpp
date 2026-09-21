// src/math.hpp
#pragma once
#include "../sdl3/sdl3.hpp"
#include "../sdl3/stdinc.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>

namespace math {

/**
 * @defgroup CategoryMath3D 3D Mathematics
 *
 * Vectors (FVector2, FVector3, FVector4), FMatrix4, FQuaternion, FAABB, FPlane, FRay, and FFrustum.
 *
 * @{
 */

// ── Forward declarations ───────────────────────────────────────────────────────

struct FVector2;
struct FVector3;
struct FVector4;
struct FMatrix4;
struct FQuaternion;
struct FAABB;
struct FPlane;
struct FRay;
struct FFrustum;

// ============================================================================
// Corners / Sides — rayons de coins et épaisseurs de bords (types géométriques
// génériques : bordures arrondies du Renderer, padding/marges du module ui...)
//
// Définition canonique déplacée vers sdl3::Corners/sdl3::Sides
// (sdl3/structs.hpp) : sdl3::render.hpp (inclus via sdl3.hpp, lui-même inclus
// juste en dessous) les utilise, et sdl3.hpp est inclus en TOUT DÉBUT de ce
// fichier (voir la ligne #include tout en haut) — définir ces types ici même
// créerait un cycle (render.hpp les utiliserait avant que ce fichier ait eu
// la chance de les déclarer). De simples alias suffisent : tout le code
// existant qui écrit math::Corners/math::Sides continue de compiler
// à l'identique.
// ============================================================================

using Corners = sdl3::Corners;
using Sides = sdl3::Sides;

// ── FVector2 ───────────────────────────────────────────────────────────────────────

/**
 * 2D floating-point vector.
 */
struct FVector2 : sdl3::FPoint {

	// ── Constructors ──────────────────────────────────────────────────────
	constexpr FVector2() noexcept = default;
	constexpr FVector2(float x, float y) noexcept : sdl3::FPoint(x, y) {}
	explicit constexpr FVector2(float s) noexcept : sdl3::FPoint(s, s) {}
	constexpr FVector2(const sdl3::FPoint &p) noexcept : sdl3::FPoint(p.x, p.y) {}

	// ── Math operations ───────────────────────────────────────────────────

	/// dot product.
	[[nodiscard]] constexpr float Dot(const FVector2 &o) const noexcept { return x * o.x + y * o.y; }

	/// Squared length (avoids sqrt).
	[[nodiscard]] constexpr float LengthSq() const noexcept { return x * x + y * y; }

	/// Euclidean length.
	[[nodiscard]] float Length() const noexcept { return sdl3::Sqrt(LengthSq()); }

	/// Normalised copy (unit length). Returns Zero vector when near-Zero.
	[[nodiscard]] FVector2 Normalize() const noexcept {
		float len = Length();
		return (len > 1e-8f) ? FVector2(*this / len) : FVector2{};
	}

	/// Linear interpolation toward `to` by factor `t`.
	[[nodiscard]] constexpr FVector2 Lerp(const FVector2 &to, float t) const noexcept {
		return {x + (to.x - x) * t, y + (to.y - y) * t};
	}

	/// Squared Euclidean distance to `o`.
	[[nodiscard]] constexpr float DistanceSq(const FVector2 &o) const noexcept {
		const float dx = x - o.x, dy = y - o.y;
		return dx * dx + dy * dy;
	}

	/// Euclidean distance to `o`.
	[[nodiscard]] float Distance(const FVector2 &o) const noexcept { return sdl3::Sqrt(DistanceSq(o)); }
};

[[nodiscard]] constexpr FVector2 operator*(float s, const FVector2 &v) noexcept { return FVector2(v * s); }
[[nodiscard]] constexpr FVector2 operator/(float s, const FVector2 &v) noexcept { return FVector2(v * (1 / s)); }

// ── FVector3 ───────────────────────────────────────────────────────────────────────

/**
 * 3D floating-point vector.
 */
struct FVector3 {
	float x = 0.f, y = 0.f, z = 0.f;

	// ── Constructors ──────────────────────────────────────────────────────
	constexpr FVector3() noexcept = default;
	constexpr FVector3(float x, float y, float z) noexcept : x{x}, y{y}, z{z} {}
	explicit constexpr FVector3(float s) noexcept : x{s}, y{s}, z{s} {}
	constexpr FVector3(const FVector2 &v, float z = 0.f) noexcept : x{v.x}, y{v.y}, z{z} {}

	// ── Arithmetic ────────────────────────────────────────────────────────
	[[nodiscard]] constexpr FVector3 operator+(const FVector3 &o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
	[[nodiscard]] constexpr FVector3 operator-(const FVector3 &o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
	[[nodiscard]] constexpr FVector3 operator*(const FVector3 &o) const noexcept { return {x * o.x, y * o.y, z * o.z}; }
	[[nodiscard]] constexpr FVector3 operator/(const FVector3 &o) const noexcept { return {x / o.x, y / o.y, z / o.z}; }
	[[nodiscard]] constexpr FVector3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }
	[[nodiscard]] constexpr FVector3 operator/(float s) const noexcept { return {x / s, y / s, z / s}; }
	[[nodiscard]] constexpr FVector3 operator-() const noexcept { return {-x, -y, -z}; }

	constexpr FVector3 &operator+=(const FVector3 &o) noexcept {
		x += o.x;
		y += o.y;
		z += o.z;
		return *this;
	}
	constexpr FVector3 &operator-=(const FVector3 &o) noexcept {
		x -= o.x;
		y -= o.y;
		z -= o.z;
		return *this;
	}
	constexpr FVector3 &operator*=(float s) noexcept {
		x *= s;
		y *= s;
		z *= s;
		return *this;
	}
	constexpr FVector3 &operator/=(float s) noexcept {
		x /= s;
		y /= s;
		z /= s;
		return *this;
	}

	[[nodiscard]] constexpr bool operator==(const FVector3 &o) const noexcept {
		return x == o.x && y == o.y && z == o.z;
	}
	[[nodiscard]] constexpr bool operator!=(const FVector3 &o) const noexcept { return !(*this == o); }

	// ── Math operations ───────────────────────────────────────────────────

	/// dot product.
	[[nodiscard]] constexpr float Dot(const FVector3 &o) const noexcept { return x * o.x + y * o.y + z * o.z; }

	/// cross product (`this × o`).
	[[nodiscard]] constexpr FVector3 Cross(const FVector3 &o) const noexcept {
		return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
	}

	/// Squared length (avoids sqrt).
	[[nodiscard]] constexpr float LengthSq() const noexcept { return x * x + y * y + z * z; }

	/// Euclidean length.
	[[nodiscard]] float Length() const noexcept { return sdl3::Sqrt(LengthSq()); }

	/// Normalised copy (unit length). Returns Zero vector when near-Zero.
	[[nodiscard]] FVector3 Normalize() const noexcept {
		float len = Length();
		return len > 1e-8f ? (*this / len) : FVector3{};
	}

	/// Linear interpolation toward `to` by factor `t`.
	[[nodiscard]] constexpr FVector3 Lerp(const FVector3 &to, float t) const noexcept {
		return {x + (to.x - x) * t, y + (to.y - y) * t, z + (to.z - z) * t};
	}

	/// Reflect this vector about a unit normal `n`.
	[[nodiscard]] constexpr FVector3 Reflect(const FVector3 &n) const noexcept { return *this - n * (2.f * Dot(n)); }

	/// Squared Euclidean distance to `o`.
	[[nodiscard]] constexpr float DistanceSq(const FVector3 &o) const noexcept { return (*this - o).LengthSq(); }

	/// Euclidean distance to `o`.
	[[nodiscard]] float Distance(const FVector3 &o) const noexcept { return (*this - o).Length(); }

	/// Component-wise minimum.
	[[nodiscard]] static constexpr FVector3 Min(const FVector3 &a, const FVector3 &b) noexcept {
		return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z};
	}

	/// Component-wise maximum.
	[[nodiscard]] static constexpr FVector3 Max(const FVector3 &a, const FVector3 &b) noexcept {
		return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z};
	}

	/// XY components.
	[[nodiscard]] constexpr FVector2 XY() const noexcept { return {x, y}; }
};

[[nodiscard]] constexpr FVector3 operator*(float s, const FVector3 &v) noexcept { return v * s; }
[[nodiscard]] constexpr FVector3 operator/(float s, const FVector3 &v) noexcept { return v * (1 / s); }

// ── FVector4 ───────────────────────────────────────────────────────────────────────

/**
 * 4D floating-point vector (homogeneous position, colour, or general use).
 *
 * Default `w = 0` (direction / colour); use `FVector4(v, 1.f)` for a position.
 */
struct FVector4 {
	float x = 0.f, y = 0.f, z = 0.f, w = 0.f;

	// ── Constructors ──────────────────────────────────────────────────────
	constexpr FVector4() noexcept = default;
	constexpr FVector4(float x, float y, float z, float w = 0.f) noexcept : x{x}, y{y}, z{z}, w{w} {}
	explicit constexpr FVector4(float s) noexcept : x{s}, y{s}, z{s}, w{s} {}
	constexpr FVector4(const FVector3 &v, float w = 0.f) noexcept : x{v.x}, y{v.y}, z{v.z}, w{w} {}
	constexpr FVector4(const FVector2 &v, float z = 0.f, float w = 0.f) noexcept : x{v.x}, y{v.y}, z{z}, w{w} {}

	// ── Arithmetic ────────────────────────────────────────────────────────
	[[nodiscard]] constexpr FVector4 operator+(const FVector4 &o) const noexcept {
		return {x + o.x, y + o.y, z + o.z, w + o.w};
	}
	[[nodiscard]] constexpr FVector4 operator-(const FVector4 &o) const noexcept {
		return {x - o.x, y - o.y, z - o.z, w - o.w};
	}
	[[nodiscard]] constexpr FVector4 operator*(float s) const noexcept { return {x * s, y * s, z * s, w * s}; }
	[[nodiscard]] constexpr FVector4 operator/(float s) const noexcept { return {x / s, y / s, z / s, w / s}; }
	[[nodiscard]] constexpr FVector4 operator-() const noexcept { return {-x, -y, -z, -w}; }

	constexpr FVector4 &operator+=(const FVector4 &o) noexcept {
		x += o.x;
		y += o.y;
		z += o.z;
		w += o.w;
		return *this;
	}
	constexpr FVector4 &operator-=(const FVector4 &o) noexcept {
		x -= o.x;
		y -= o.y;
		z -= o.z;
		w -= o.w;
		return *this;
	}
	constexpr FVector4 &operator*=(float s) noexcept {
		x *= s;
		y *= s;
		z *= s;
		w *= s;
		return *this;
	}
	constexpr FVector4 &operator/=(float s) noexcept {
		x /= s;
		y /= s;
		z /= s;
		w /= s;
		return *this;
	}

	[[nodiscard]] constexpr bool operator==(const FVector4 &o) const noexcept {
		return x == o.x && y == o.y && z == o.z && w == o.w;
	}
	[[nodiscard]] constexpr bool operator!=(const FVector4 &o) const noexcept { return !(*this == o); }

	// ── Math operations ───────────────────────────────────────────────────

	/// 4D dot product.
	[[nodiscard]] constexpr float Dot(const FVector4 &o) const noexcept {
		return x * o.x + y * o.y + z * o.z + w * o.w;
	}

	/// Squared length.
	[[nodiscard]] constexpr float LengthSq() const noexcept { return x * x + y * y + z * z + w * w; }

	/// Euclidean length.
	[[nodiscard]] float Length() const noexcept { return sdl3::Sqrt(LengthSq()); }

	/// Normalised copy. Returns Zero vector when near-Zero.
	[[nodiscard]] FVector4 Normalize() const noexcept {
		float len = Length();
		return len > 1e-8f ? (*this / len) : FVector4{};
	}

	/// Perspective division: `{x/w, y/w, z/w}`.
	[[nodiscard]] FVector3 perspDiv() const noexcept { return {x / w, y / w, z / w}; }

	/// Linear interpolation toward `to` by factor `t`.
	[[nodiscard]] constexpr FVector4 Lerp(const FVector4 &to, float t) const noexcept {
		return {x + (to.x - x) * t, y + (to.y - y) * t, z + (to.z - z) * t, w + (to.w - w) * t};
	}

	/// XYZ components.
	[[nodiscard]] constexpr FVector3 XYZ() const noexcept { return {x, y, z}; }

	/// XY components.
	[[nodiscard]] constexpr FVector2 XY() const noexcept { return {x, y}; }
};

[[nodiscard]] constexpr FVector4 operator*(float s, const FVector4 &v) noexcept { return v * s; }
[[nodiscard]] constexpr FVector4 operator/(float s, const FVector4 &v) noexcept { return v * (1 / s); }

// ── FMatrix4 ───────────────────────────────────────────────────────────────────────

/**
 * Column-major 4×4 floating-point matrix.
 *
 * Storage layout: `m[col * 4 + row]` — same convention as GLSL `mat4`.
 *
 * All projection functions target Vulkan clip space (Y-down, Z ∈ [0, 1]).
 */
struct FMatrix4 {
	float m[16] = {};

	// ── Constructors ──────────────────────────────────────────────────────
	constexpr FMatrix4() noexcept = default;

	// ── Element access ────────────────────────────────────────────────────

	/// Element At row `r`, column `c`.
	[[nodiscard]] constexpr float &At(int r, int c) noexcept { return m[c * 4 + r]; }
	[[nodiscard]] constexpr float At(int r, int c) const noexcept { return m[c * 4 + r]; }

	// ── Factory methods ───────────────────────────────────────────────────

	/// Identity matrix.
	[[nodiscard]] static constexpr FMatrix4 Identity() noexcept {
		FMatrix4 r;
		r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
		return r;
	}

	/**
	 * Translation matrix.
	 *
	 * @param x,y,z Translation components.
	 */
	[[nodiscard]] static constexpr FMatrix4 Translate(float x, float y, float z) noexcept {
		FMatrix4 r = Identity();
		r.m[12] = x;
		r.m[13] = y;
		r.m[14] = z;
		return r;
	}

	/// Translation matrix from a FVector3.
	[[nodiscard]] static constexpr FMatrix4 Translate(const FVector3 &t) noexcept { return Translate(t.x, t.y, t.z); }

	/**
	 * Non-uniform Scale matrix.
	 *
	 * @param x,y,z Scale factors per axis.
	 */
	[[nodiscard]] static constexpr FMatrix4 Scale(float x, float y, float z) noexcept {
		FMatrix4 r;
		r.m[0] = x;
		r.m[5] = y;
		r.m[10] = z;
		r.m[15] = 1.f;
		return r;
	}

	/// Uniform Scale matrix.
	[[nodiscard]] static constexpr FMatrix4 Scale(float s) noexcept { return Scale(s, s, s); }

	/// Scale matrix from a FVector3.
	[[nodiscard]] static constexpr FMatrix4 Scale(const FVector3 &s) noexcept { return Scale(s.x, s.y, s.z); }

	/**
	 * Rotation around the X axis.
	 *
	 * @param a Angle in radians (right-hand rule: thumb along +X).
	 */
	[[nodiscard]] static FMatrix4 RotateX(float a) noexcept {
		float c = sdl3::Cos(a), s = sdl3::Sin(a);
		FMatrix4 r = Identity();
		r.m[5] = c;
		r.m[6] = s;
		r.m[9] = -s;
		r.m[10] = c;
		return r;
	}

	/**
	 * Rotation around the Y axis.
	 *
	 * @param a Angle in radians.
	 */
	[[nodiscard]] static FMatrix4 RotateY(float a) noexcept {
		float c = sdl3::Cos(a), s = sdl3::Sin(a);
		FMatrix4 r = Identity();
		r.m[0] = c;
		r.m[2] = -s;
		r.m[8] = s;
		r.m[10] = c;
		return r;
	}

	/**
	 * Rotation around the Z axis.
	 *
	 * @param a Angle in radians.
	 */
	[[nodiscard]] static FMatrix4 RotateZ(float a) noexcept {
		float c = sdl3::Cos(a), s = sdl3::Sin(a);
		FMatrix4 r = Identity();
		r.m[0] = c;
		r.m[1] = s;
		r.m[4] = -s;
		r.m[5] = c;
		return r;
	}

	/**
	 * Rotation around an arbitrary (normalised) axis — Rodrigues' formula.
	 *
	 * @param axis  Normalised rotation axis.
	 * @param angle Angle in radians.
	 */
	[[nodiscard]] static FMatrix4 Rotate(const FVector3 &axis, float angle) noexcept {
		float c = sdl3::Cos(angle);
		float s = sdl3::Sin(angle);
		float t = 1.f - c;
		float x = axis.x, y = axis.y, z = axis.z;

		FMatrix4 r;
		// Column 0
		r.m[0] = t * x * x + c;
		r.m[1] = t * x * y + s * z;
		r.m[2] = t * x * z - s * y;
		r.m[3] = 0.f;
		// Column 1
		r.m[4] = t * x * y - s * z;
		r.m[5] = t * y * y + c;
		r.m[6] = t * y * z + s * x;
		r.m[7] = 0.f;
		// Column 2
		r.m[8] = t * x * z + s * y;
		r.m[9] = t * y * z - s * x;
		r.m[10] = t * z * z + c;
		r.m[11] = 0.f;
		// Column 3
		r.m[12] = 0.f;
		r.m[13] = 0.f;
		r.m[14] = 0.f;
		r.m[15] = 1.f;
		return r;
	}

	/**
	 * Perspective projection (right-handed, Vulkan clip space).
	 *
	 * Maps the view frustum to clip space with Y-down NDC and Z ∈ [0, 1].
	 *
	 * @param fovY   Vertical field-of-view in radians.
	 * @param aspect Width / height ratio.
	 * @param nearZ  Near clip distance (positive, > 0).
	 * @param farZ   Far clip distance (positive, > nearZ).
	 */
	[[nodiscard]] static FMatrix4 Perspective(float fovY, float aspect, float nearZ, float farZ) noexcept {
		float f = 1.f / sdl3::Tan(fovY * 0.5f);
		FMatrix4 r;
		r.m[0] = f / aspect;
		// PAS de négation ici. SDL_GPU normalise les coordonnées normalisées
		// d'appareil sur la convention D3D12/Metal — **+Y vers le HAUT**, Z
		// dans [0,1] — et effectue lui-même la conversion pour les pilotes qui
		// diffèrent, Vulkan compris (SDL_gpu.h, section « Coordinate System » :
		// « you don't need to perform any coordinate flipping logic »). Le
		// `-f` qui se trouvait ici, commenté « flip Y for Vulkan NDC »,
		// ajoutait donc un SECOND retournement à celui de SDL : toute scène
		// 3D sortait verticalement inversée.
		r.m[5] = f;
		r.m[10] = farZ / (nearZ - farZ); // near→0, far→1
		r.m[11] = -1.f;
		r.m[14] = nearZ * farZ / (nearZ - farZ);
		return r;
	}

	/**
	 * Orthographic projection (right-handed, Vulkan clip space).
	 *
	 * Maps the view box to clip space with Y-down NDC and Z ∈ [0, 1].
	 *
	 * @param left,right  X bounds in view space.
	 * @param bottom,top  Y bounds in view space.
	 * @param nearZ,farZ  Positive clip distances from the eye.
	 */
	[[nodiscard]] static constexpr FMatrix4 Ortho(float left, float right, float bottom, float top, float nearZ,
												  float farZ) noexcept {
		FMatrix4 r;
		r.m[0] = 2.f / (right - left);
		// Même correction que `Perspective` ci-dessus : +Y vers le haut, la
		// conversion éventuelle vers Vulkan étant faite par SDL_GPU. Le terme
		// de translation en Y change de signe avec l'échelle.
		r.m[5] = 2.f / (top - bottom);
		r.m[10] = -1.f / (farZ - nearZ); // near→0, far→1
		r.m[12] = -(right + left) / (right - left);
		r.m[13] = -(top + bottom) / (top - bottom);
		r.m[14] = -nearZ / (farZ - nearZ);
		r.m[15] = 1.f;
		return r;
	}

	/**
	 * View matrix (right-handed LookAt).
	 *
	 * @param eye    Camera position in ECS::Context space.
	 * @param Center Point the camera looks toward.
	 * @param up     ECS::Context up direction (typically {0, 1, 0}).
	 */
	[[nodiscard]] static FMatrix4 LookAt(const FVector3 &eye, const FVector3 &Center, const FVector3 &up) noexcept {
		FVector3 f = (Center - eye).Normalize(); // forward  (+Z in ECS::Context → −Z in view)
		FVector3 r = f.Cross(up).Normalize();    // right
		FVector3 u = r.Cross(f);                 // re-orthogonalised up

		FMatrix4 res;
		// Column 0
		res.m[0] = r.x;
		res.m[1] = u.x;
		res.m[2] = -f.x;
		res.m[3] = 0.f;
		// Column 1
		res.m[4] = r.y;
		res.m[5] = u.y;
		res.m[6] = -f.y;
		res.m[7] = 0.f;
		// Column 2
		res.m[8] = r.z;
		res.m[9] = u.z;
		res.m[10] = -f.z;
		res.m[11] = 0.f;
		// Column 3 — translation
		res.m[12] = -r.Dot(eye);
		res.m[13] = -u.Dot(eye);
		res.m[14] = f.Dot(eye);
		res.m[15] = 1.f;
		return res;
	}

	// ── Operators ─────────────────────────────────────────────────────────

	/// Column-major matrix multiplication: `C = this * b`.
	[[nodiscard]] constexpr FMatrix4 operator*(const FMatrix4 &b) const noexcept {
		FMatrix4 r;
		for (int col = 0; col < 4; ++col)
			for (int row = 0; row < 4; ++row) {
				float v = 0.f;
				for (int k = 0; k < 4; ++k)
					v += m[k * 4 + row] * b.m[col * 4 + k];
				r.m[col * 4 + row] = v;
			}
		return r;
	}

	constexpr FMatrix4 &operator*=(const FMatrix4 &b) noexcept {
		*this = *this * b;
		return *this;
	}

	/// Transform a FVector4: `this * v`.
	[[nodiscard]] constexpr FVector4 operator*(const FVector4 &v) const noexcept {
		return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w, m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
				m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
				m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
	}

	// ── Geometry helpers ──────────────────────────────────────────────────

	/**
	 * Transform a point (w = 1) and apply Perspective division.
	 *
	 * Use when the matrix may contain a projection.
	 */
	[[nodiscard]] FVector3 TransformPoint(const FVector3 &p) const noexcept {
		return (*this * FVector4{p, 1.f}).perspDiv();
	}

	/**
	 * Transform a direction (w = 0) — ignores the translation column.
	 */
	[[nodiscard]] constexpr FVector3 TransformDir(const FVector3 &d) const noexcept {
		return (*this * FVector4{d, 0.f}).XYZ();
	}

	/// Transpose.
	[[nodiscard]] constexpr FMatrix4 Transpose() const noexcept {
		FMatrix4 r;
		for (int c = 0; c < 4; ++c)
			for (int row = 0; row < 4; ++row)
				r.m[row * 4 + c] = m[c * 4 + row];
		return r;
	}

	/**
	 * General Inverse (Gauss-Jordan elimination with partial pivoting).
	 *
	 * Returns Identity() if the matrix is singular.
	 */
	[[nodiscard]] FMatrix4 Inverse() const noexcept {
		float a[4][8];
		for (int r = 0; r < 4; ++r) {
			for (int c = 0; c < 4; ++c)
				a[r][c] = m[c * 4 + r]; // row-major copy of this matrix
			for (int c = 4; c < 8; ++c)
				a[r][c] = (c - 4 == r) ? 1.f : 0.f; // augment with Identity
		}

		for (int col = 0; col < 4; ++col) {
			// Partial pivot
			int pivot = col;
			float pval = sdl3::Abs(a[col][col]);
			for (int r = col + 1; r < 4; ++r) {
				if (sdl3::Abs(a[r][col]) > pval) {
					pval = sdl3::Abs(a[r][col]);
					pivot = r;
				}
			}
			if (pivot != col)
				for (int c = 0; c < 8; ++c)
					std::swap(a[col][c], a[pivot][c]);

			float diag = a[col][col];
			if (sdl3::Abs(diag) < 1e-8f)
				return Identity(); // singular

			float inv = 1.f / diag;
			for (int c = 0; c < 8; ++c)
				a[col][c] *= inv;

			for (int r = 0; r < 4; ++r) {
				if (r == col)
					continue;
				float f = a[r][col];
				for (int c = 0; c < 8; ++c)
					a[r][c] -= f * a[col][c];
			}
		}

		FMatrix4 res;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				res.m[c * 4 + r] = a[r][c + 4];
		return res;
	}

	/// Raw pointer to the 16 floats — for GPU uploads.
	[[nodiscard]] const float *Data() const noexcept { return m; }
	[[nodiscard]] float *Data() noexcept { return m; }
};

// ── FQuaternion ────────────────────────────────────────────────────────────────

/**
 * Unit quaternion representing a 3D rotation.
 *
 * Stored as `(x, y, z, w)` where `w` is the scalar part.
 * Assumes the quaternion is unit-length for rotation operations.
 */
struct FQuaternion {
	float x = 0.f, y = 0.f, z = 0.f, w = 1.f;

	// ── Constructors ──────────────────────────────────────────────────────
	constexpr FQuaternion() noexcept = default;
	constexpr FQuaternion(float x, float y, float z, float w) noexcept : x{x}, y{y}, z{z}, w{w} {}

	// ── Factory methods ───────────────────────────────────────────────────

	/// Identity quaternion (no rotation).
	[[nodiscard]] static constexpr FQuaternion Identity() noexcept { return {0.f, 0.f, 0.f, 1.f}; }

	/**
	 * Build from a normalised axis and an angle.
	 *
	 * @param axis  Normalised rotation axis.
	 * @param angle Rotation angle in radians.
	 */
	[[nodiscard]] static FQuaternion FromAxisAngle(const FVector3 &axis, float angle) noexcept {
		float s = sdl3::Sin(angle * 0.5f);
		float c = sdl3::Cos(angle * 0.5f);
		return {axis.x * s, axis.y * s, axis.z * s, c};
	}

	/**
	 * Build from Euler angles (ZXY convention: roll, then pitch, then yaw).
	 *
	 * @param pitch Rotation around X in radians.
	 * @param yaw   Rotation around Y in radians.
	 * @param roll  Rotation around Z in radians.
	 */
	[[nodiscard]] static FQuaternion FromEuler(float pitch, float yaw, float roll) noexcept {
		float cp = sdl3::Cos(pitch * 0.5f), sp = sdl3::Sin(pitch * 0.5f);
		float cy = sdl3::Cos(yaw * 0.5f), sy = sdl3::Sin(yaw * 0.5f);
		float cr = sdl3::Cos(roll * 0.5f), sr = sdl3::Sin(roll * 0.5f);
		return {cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy, sr * cp * cy - cr * sp * sy,
				cr * cp * cy + sr * sp * sy};
	}

	/**
	 * Build the shortest rotation from direction `from` to direction `to`
	 * (both should be normalised).
	 */
	[[nodiscard]] static FQuaternion FromTo(const FVector3 &from, const FVector3 &to) noexcept {
		FVector3 axis = from.Cross(to);
		float dot = from.Dot(to);
		// Cos(θ/2) = sqrt((1+dot)/2),  Sin(θ/2) = |axis| / (2 * Cos(θ/2))
		float w = sdl3::Sqrt((1.f + dot) * 0.5f);
		float s = (w > 1e-8f) ? 0.5f / w : 0.f;
		return FQuaternion{axis.x * s, axis.y * s, axis.z * s, w}.Normalize();
	}

	// ── Operations ────────────────────────────────────────────────────────

	/// Hamilton product.
	[[nodiscard]] constexpr FQuaternion operator*(const FQuaternion &o) const noexcept {
		return {w * o.x + x * o.w + y * o.z - z * o.y, w * o.y - x * o.z + y * o.w + z * o.x,
				w * o.z + x * o.y - y * o.x + z * o.w, w * o.w - x * o.x - y * o.y - z * o.z};
	}

	constexpr FQuaternion &operator*=(const FQuaternion &o) noexcept {
		*this = *this * o;
		return *this;
	}

	/// Conjugate (equals Inverse for unit quaternions).
	[[nodiscard]] constexpr FQuaternion Conjugate() const noexcept { return {-x, -y, -z, w}; }

	/// Squared Norm.
	[[nodiscard]] constexpr float NormSq() const noexcept { return x * x + y * y + z * z + w * w; }

	/// Norm.
	[[nodiscard]] float Norm() const noexcept { return sdl3::Sqrt(NormSq()); }

	/// Normalised copy.
	[[nodiscard]] FQuaternion Normalize() const noexcept {
		float n = Norm();
		return n > 1e-8f ? FQuaternion{x / n, y / n, z / n, w / n} : Identity();
	}

	/// Rotate a FVector3 by this quaternion.
	[[nodiscard]] constexpr FVector3 Rotate(const FVector3 &v) const noexcept {
		FVector3 qv{x, y, z};
		FVector3 t = 2.f * qv.Cross(v);
		return v + w * t + qv.Cross(t);
	}

	/**
	 * Convert to a 4×4 rotation matrix (assumes unit quaternion).
	 */
	[[nodiscard]] constexpr FMatrix4 ToMat4() const noexcept {
		float x2 = x * x, y2 = y * y, z2 = z * z;
		float xy = x * y, xz = x * z, yz = y * z;
		float wx = w * x, wy = w * y, wz = w * z;

		FMatrix4 r;
		// Column 0
		r.m[0] = 1.f - 2.f * (y2 + z2);
		r.m[1] = 2.f * (xy + wz);
		r.m[2] = 2.f * (xz - wy);
		r.m[3] = 0.f;
		// Column 1
		r.m[4] = 2.f * (xy - wz);
		r.m[5] = 1.f - 2.f * (x2 + z2);
		r.m[6] = 2.f * (yz + wx);
		r.m[7] = 0.f;
		// Column 2
		r.m[8] = 2.f * (xz + wy);
		r.m[9] = 2.f * (yz - wx);
		r.m[10] = 1.f - 2.f * (x2 + y2);
		r.m[11] = 0.f;
		// Column 3
		r.m[12] = r.m[13] = r.m[14] = 0.f;
		r.m[15] = 1.f;
		return r;
	}

	/**
	 * Rotation carried by a 4x4 matrix whose upper-left 3x3 block is a pure
	 * rotation (orthonormal columns, no scale left in it — callers with a
	 * scaled matrix divide the columns by their length first, which is what
	 * DecomposeTRS() does below).
	 *
	 * Shepperd's method: the four possible square roots of the diagonal are
	 * computed and the LARGEST is used, because the others go through a
	 * near-zero divisor exactly where the rotation approaches 180 degrees on
	 * an axis — the classic "trace only" formula loses all its precision
	 * there and can even take a square root of a small negative number. The
	 * branch picked is therefore a numerical necessity, not an optimisation.
	 *
	 * Exact inverse of ToMat4() (same column-major convention) — verified
	 * round-trip in tests/scene_node_smoke_test.cpp.
	 */
	[[nodiscard]] static FQuaternion FromMatrix(const FMatrix4 &m) noexcept {
		const float m00 = m.m[0], m01 = m.m[4], m02 = m.m[8];
		const float m10 = m.m[1], m11 = m.m[5], m12 = m.m[9];
		const float m20 = m.m[2], m21 = m.m[6], m22 = m.m[10];
		const float trace = m00 + m11 + m22;
		FQuaternion q;
		if (trace > 0.f) {
			float s = std::sqrt(trace + 1.f) * 2.f;
			q.w = 0.25f * s;
			q.x = (m21 - m12) / s;
			q.y = (m02 - m20) / s;
			q.z = (m10 - m01) / s;
		} else if (m00 > m11 && m00 > m22) {
			float s = std::sqrt(1.f + m00 - m11 - m22) * 2.f;
			q.w = (m21 - m12) / s;
			q.x = 0.25f * s;
			q.y = (m01 + m10) / s;
			q.z = (m02 + m20) / s;
		} else if (m11 > m22) {
			float s = std::sqrt(1.f + m11 - m00 - m22) * 2.f;
			q.w = (m02 - m20) / s;
			q.x = (m01 + m10) / s;
			q.y = 0.25f * s;
			q.z = (m12 + m21) / s;
		} else {
			float s = std::sqrt(1.f + m22 - m00 - m11) * 2.f;
			q.w = (m10 - m01) / s;
			q.x = (m02 + m20) / s;
			q.y = (m12 + m21) / s;
			q.z = 0.25f * s;
		}
		return q.Normalize();
	}

	/**
	 * Exact inverse of `FromEuler()` — decomposes this rotation back into
	 * `(pitch, yaw, roll)` radians, same YXZ composition order
	 * (`q = Ry(yaw) * Rx(pitch) * Rz(roll)`, which is what FromEuler()'s
	 * formula expands to).
	 *
	 * Added because a level editor's inspector has to DISPLAY the rotation of
	 * an object that was rotated by something other than the inspector itself
	 * (a script, the physics solver, an animation clip) — without this, the
	 * only way to keep Euler fields honest was to cache the angles the user
	 * last typed and never read the quaternion back, which silently desyncs
	 * as soon as anything else writes the rotation.
	 *
	 * Gimbal lock (pitch at ±90°, where yaw and roll become the same axis) is
	 * resolved by convention: `roll` is pinned to 0 and the whole remaining
	 * rotation is attributed to `yaw`. The returned angles then still rebuild
	 * the SAME rotation through `FromEuler()`, which is the property that
	 * actually matters — but they are not necessarily the angles originally
	 * passed in, because at that singularity infinitely many (yaw, roll)
	 * pairs describe the identical rotation.
	 *
	 * @return `{pitch, yaw, roll}` in radians — pitch in [-π/2, π/2], yaw and
	 *         roll in [-π, π].
	 */
	[[nodiscard]] FVector3 ToEuler() const noexcept {
		// Termes de la matrice de rotation (convention ligne/colonne
		// standard ; cf. ToMat4() ci-dessus, stockée en colonnes).
		const float r12 = 2.f * (y * z - w * x); // R[1][2] = -sin(pitch)
		const float sinPitch = sdl3::Clamp(-r12, -1.f, 1.f);
		const float pitch = sdl3::Asin(sinPitch);

		// cos(pitch) ≈ 0 => blocage de cardan : yaw et roll tournent autour
		// du même axe, on impute tout au yaw (cf. doc ci-dessus).
		if (sdl3::Abs(sinPitch) > 0.99999f) {
			const float yawLocked = sdl3::Atan2(2.f * (w * y - x * z), 1.f - 2.f * (y * y + z * z));
			return {pitch, yawLocked, 0.f};
		}

		const float roll = sdl3::Atan2(2.f * (x * y + w * z), 1.f - 2.f * (x * x + z * z));
		const float yaw = sdl3::Atan2(2.f * (x * z + w * y), 1.f - 2.f * (x * x + y * y));
		return {pitch, yaw, roll};
	}

	/**
	 * Spherical linear interpolation (SLERP) from this quaternion toward `to`.
	 *
	 * @param to Target quaternion (unit length).
	 * @param t  Interpolation factor in [0, 1].
	 */
	[[nodiscard]] FQuaternion Slerp(const FQuaternion &to, float t) const noexcept {
		float dot = x * to.x + y * to.y + z * to.z + w * to.w;
		FQuaternion end = to;
		if (dot < 0.f) {
			dot = -dot;
			end = {-to.x, -to.y, -to.z, -to.w};
		} // shortest path
		dot = sdl3::Clamp(dot, -1.f, 1.f);

		if (dot > 0.9995f) {
			// Quaternions nearly parallel → linear interpolation
			return FQuaternion{x + t * (end.x - x), y + t * (end.y - y), z + t * (end.z - z), w + t * (end.w - w)}
				.Normalize();
		}

		float theta0 = sdl3::Acos(dot);
		float theta = theta0 * t;
		float sinTheta0 = sdl3::Sin(theta0);
		float s0 = sdl3::Cos(theta) - dot * sdl3::Sin(theta) / sinTheta0;
		float s1 = sdl3::Sin(theta) / sinTheta0;

		return FQuaternion{s0 * x + s1 * end.x, s0 * y + s1 * end.y, s0 * z + s1 * end.z, s0 * w + s1 * end.w}
			.Normalize();
	}
};

/**
 * Compose a transform matrix from translation, rotation and Scale (T * R * S).
 *
 * Applied to a point as `M * p`, the Scale acts first, then the rotation, then
 * the translation — the standard SRT order used by scene graphs.
 */
[[nodiscard]] inline FMatrix4 ComposeTRS(const FVector3 &translation, const FQuaternion &rotation,
										 const FVector3 &Scale) noexcept {
	FMatrix4 r = rotation.ToMat4();
	// Scale the rotation columns (column-major: column c is m[c*4 + row]).
	r.m[0] *= Scale.x;
	r.m[1] *= Scale.x;
	r.m[2] *= Scale.x;
	r.m[4] *= Scale.y;
	r.m[5] *= Scale.y;
	r.m[6] *= Scale.y;
	r.m[8] *= Scale.z;
	r.m[9] *= Scale.z;
	r.m[10] *= Scale.z;
	// Set translation column.
	r.m[12] = translation.x;
	r.m[13] = translation.y;
	r.m[14] = translation.z;
	return r;
}

/**
 * Inverse of ComposeTRS(): splits a transform matrix back into translation,
 * rotation and scale. Written for scene graphs, where it answers exactly two
 * questions: "what local transform gives this node that world transform?"
 * (see scene::NodeTree::SetGlobalTransform) and "what local transform keeps
 * this node where it is once reparented?" (KeepGlobal reparenting).
 *
 * Limits, stated rather than hidden — a general 4x4 is not always a TRS:
 *  - a NEGATIVE scale cannot be told apart from an extra 180 degree rotation
 *    by looking at column lengths alone, so the determinant's sign is put on
 *    the X axis (the usual convention, and the one that makes
 *    Compose(Decompose(m)) == m again);
 *  - SHEAR (non-perpendicular columns, e.g. a non-uniformly scaled parent
 *    times a rotated child) cannot be represented by a TRS at all; the
 *    rotation returned is then the orthonormalised frame, which is the
 *    closest TRS, and `sheared` reports it so a caller can warn instead of
 *    silently drifting;
 *  - a ZERO scale on an axis leaves that column undefined; the axis falls
 *    back to the identity one so the rotation stays valid.
 */
struct TRS {
	FVector3 translation;
	FQuaternion rotation = FQuaternion::Identity();
	FVector3 scale{1.f, 1.f, 1.f};
	bool sheared = false; ///< true when the matrix was not a pure TRS (see above)
};

[[nodiscard]] inline TRS DecomposeTRS(const FMatrix4 &m) noexcept {
	TRS out;
	out.translation = {m.m[12], m.m[13], m.m[14]};

	FVector3 cx{m.m[0], m.m[1], m.m[2]};
	FVector3 cy{m.m[4], m.m[5], m.m[6]};
	FVector3 cz{m.m[8], m.m[9], m.m[10]};

	float sx = cx.Length(), sy = cy.Length(), sz = cz.Length();

	// Miroir : une matrice de déterminant négatif contient un nombre IMPAIR
	// d'axes inversés ; on le porte sur X, comme le font glTF et three.js.
	// Déterminant du bloc 3x3 = produit mixte de ses colonnes. Seule
	// l'ÉCHELLE change de signe : l'axe unitaire reste `cx / sx`, donc
	// inchangé (négliger ce point donne une rotation miroir, erreur
	// réellement commise et attrapée par le test d'aller-retour).
	if (cx.Cross(cy).Dot(cz) < 0.f)
		sx = -sx;

	constexpr float EPS = 1e-8f;
	FVector3 ux = sdl3::Abs(sx) > EPS ? (1.f / sx) * cx : FVector3{1.f, 0.f, 0.f};
	FVector3 uy = sy > EPS ? (1.f / sy) * cy : FVector3{0.f, 1.f, 0.f};
	FVector3 uz = sz > EPS ? (1.f / sz) * cz : FVector3{0.f, 0.f, 1.f};

	// Cisaillement : les colonnes d'une rotation pure sont orthogonales.
	out.sheared = sdl3::Abs(ux.Dot(uy)) > 1e-3f || sdl3::Abs(ux.Dot(uz)) > 1e-3f || sdl3::Abs(uy.Dot(uz)) > 1e-3f;

	FMatrix4 rot = FMatrix4::Identity();
	rot.m[0] = ux.x;
	rot.m[1] = ux.y;
	rot.m[2] = ux.z;
	rot.m[4] = uy.x;
	rot.m[5] = uy.y;
	rot.m[6] = uy.z;
	rot.m[8] = uz.x;
	rot.m[9] = uz.y;
	rot.m[10] = uz.z;

	out.rotation = FQuaternion::FromMatrix(rot);
	out.scale = {sx, sy, sz};
	return out;
}

// ── FAABB ───────────────────────────────────────────────────────────────────────

/**
 * Axis-Aligned Bounding Sides in 3D space.
 *
 * The default-constructed FAABB is "empty" (min > max). Use `Expand()` to
 * grow it around a set of points.
 */
struct FAABB {
	FVector3 min{1e30f, 1e30f, 1e30f};
	FVector3 max{-1e30f, -1e30f, -1e30f};

	constexpr FAABB() noexcept = default;

	/// Construct from explicit min and max corners.
	constexpr FAABB(const FVector3 &min, const FVector3 &max) noexcept : min{min}, max{max} {}

	// ── Queries ───────────────────────────────────────────────────────────

	/// True when min ≤ max on all axes (non-empty).
	[[nodiscard]] constexpr bool IsValid() const noexcept { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

	/// Centre of the box.
	[[nodiscard]] constexpr FVector3 Center() const noexcept {
		return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
	}

	/// Half-extents: `(max − min) / 2`.
	[[nodiscard]] constexpr FVector3 HalfExtents() const noexcept {
		return {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f};
	}

	/// Size: `max − min`.
	[[nodiscard]] constexpr FVector3 Size() const noexcept { return max - min; }

	/// Test whether point `p` is inside (inclusive).
	[[nodiscard]] constexpr bool Contains(const FVector3 &p) const noexcept {
		return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
	}

	/// Test whether FAABB `o` is fully contained.
	[[nodiscard]] constexpr bool Contains(const FAABB &o) const noexcept { return Contains(o.min) && Contains(o.max); }

	/// Test whether this FAABB overlaps with `o`.
	[[nodiscard]] constexpr bool Intersects(const FAABB &o) const noexcept {
		return max.x >= o.min.x && min.x <= o.max.x && max.y >= o.min.y && min.y <= o.max.y && max.z >= o.min.z &&
			   min.z <= o.max.z;
	}

	// ── Mutation ──────────────────────────────────────────────────────────

	/**
	 * Expand to include point `p`.
	 *
	 * The two comparisons per axis are INDEPENDENT `if`s, not an `if/else if`.
	 * With `else if`, the very first point inserted into a default-constructed
	 * box (min = +1e30, max = -1e30) took the `p < min` branch and skipped the
	 * `p > max` one, leaving `max` at -1e30: the box stayed INVALID after
	 * inserting a point, and any sequence of decreasing points never updated
	 * `max` at all. Every user of this function was affected — including
	 * `physics::Capsule::WorldAABB()` (which expands from its two endpoints)
	 * and mesh bounds for frustum culling, where it silently culled
	 * everything.
	 */
	constexpr void Expand(const FVector3 &p) noexcept {
		if (p.x < min.x)
			min.x = p.x;
		if (p.x > max.x)
			max.x = p.x;
		if (p.y < min.y)
			min.y = p.y;
		if (p.y > max.y)
			max.y = p.y;
		if (p.z < min.z)
			min.z = p.z;
		if (p.z > max.z)
			max.z = p.z;
	}

	/// Expand to enclose `o`.
	constexpr void Expand(const FAABB &o) noexcept {
		Expand(o.min);
		Expand(o.max);
	}

	// ── Transforms ────────────────────────────────────────────────────────

	/// Return a copy Translated by `t`.
	[[nodiscard]] constexpr FAABB Translated(const FVector3 &t) const noexcept { return {min + t, max + t}; }

	/**
	 * FAABB of this box after applying matrix `mat`.
	 *
	 * Transforms all 8 corners and wraps the result.
	 */
	[[nodiscard]] FAABB Transformed(const FMatrix4 &mat) const noexcept {
		const FVector3 corners[8] = {{min.x, min.y, min.z}, {max.x, min.y, min.z}, {min.x, max.y, min.z},
									 {max.x, max.y, min.z}, {min.x, min.y, max.z}, {max.x, min.y, max.z},
									 {min.x, max.y, max.z}, {max.x, max.y, max.z}};
		FAABB result;
		for (const auto &c : corners)
			result.Expand(mat.TransformPoint(c));
		return result;
	}
};

// ── FPlane ──────────────────────────────────────────────────────────────────────

/**
 * Infinite plane: `normal · point + d = 0`.
 *
 * `normal` should be unit length for the signed distance to be in ECS::Context units.
 */
struct FPlane {
	FVector3 normal{0.f, 1.f, 0.f};
	float d = 0.f;

	constexpr FPlane() noexcept = default;

	/// Construct from a unit normal and the plane constant `d`.
	constexpr FPlane(const FVector3 &normal, float d) noexcept : normal{normal}, d{d} {}

	/// Construct from a unit normal and a point on the plane.
	FPlane(const FVector3 &n, const FVector3 &point) noexcept {
		normal = n.Normalize();
		d = -normal.Dot(point);
	}

	/// Construct from three points (CCW winding → normal toward viewer).
	[[nodiscard]] static FPlane fromTriangle(const FVector3 &a, const FVector3 &b, const FVector3 &c) noexcept {
		FVector3 n = (b - a).Cross(c - a).Normalize();
		return {n, -n.Dot(a)};
	}

	/// Signed distance from `p` to the plane (positive = same side as normal).
	[[nodiscard]] constexpr float Distance(const FVector3 &p) const noexcept { return normal.Dot(p) + d; }

	/// Normalised copy (unit-length normal).
	[[nodiscard]] FPlane Normalize() const noexcept {
		float len = normal.Length();
		return len > 1e-8f ? FPlane{normal / len, d / len} : *this;
	}
};

// ── FRay ────────────────────────────────────────────────────────────────────────

/**
 * FRay: a half-line with an origin and a (normalised) direction.
 */
struct FRay {
	FVector3 origin;
	FVector3 direction{0.f, 0.f, -1.f}; // default: looking down −Z

	constexpr FRay() noexcept = default;

	/// Construct from an origin and a direction (automatically normalised).
	FRay(const FVector3 &origin, const FVector3 &direction) noexcept
		: origin{origin}, direction{direction.Normalize()} {}

	/// Point on the ray At parameter `t`: `origin + direction * t`.
	[[nodiscard]] constexpr FVector3 At(float t) const noexcept {
		return {origin.x + direction.x * t, origin.y + direction.y * t, origin.z + direction.z * t};
	}

	/**
	 * Shortest distance from `point` to this ray — the HALF-line, not the
	 * infinite line: a point behind `origin` is measured from `origin`, so a
	 * hit test never matches something behind the viewer. `direction` being
	 * unit length (the constructor normalises it), the projection parameter
	 * is a plain dot product.
	 */
	[[nodiscard]] float DistanceTo(const FVector3 &point) const noexcept {
		const float t = sdl3::Max((point - origin).Dot(direction), 0.f);
		return (point - At(t)).Length();
	}

	/**
	 * Slab-test intersection with an FAABB.
	 *
	 * @param box       FAABB to test.
	 * @param tMin [out] Entry parameter (may be negative if origin is inside).
	 * @param tMax [out] Exit parameter.
	 * @returns true if the ray Intersects the box.
	 */
	[[nodiscard]] bool Intersects(const FAABB &box, float &tMin, float &tMax) const noexcept {
		tMin = 0.f;
		tMax = 1e30f;

		for (int i = 0; i < 3; ++i) {
			float orig = (&origin.x)[i];
			float dir = (&direction.x)[i];
			float bmin = (&box.min.x)[i];
			float bmax = (&box.max.x)[i];

			if (sdl3::Abs(dir) < 1e-8f) {
				if (orig < bmin || orig > bmax)
					return false;
			} else {
				float t1 = (bmin - orig) / dir;
				float t2 = (bmax - orig) / dir;
				if (t1 > t2)
					std::swap(t1, t2);
				tMin = sdl3::Max(tMin, t1);
				tMax = sdl3::Min(tMax, t2);
				if (tMin > tMax)
					return false;
			}
		}
		return true;
	}

	/**
	 * Intersection with a plane.
	 *
	 * @param plane  FPlane to test.
	 * @param t [out] distance along the ray to the intersection.
	 * @returns true if the ray hits the plane (not parallel, in front).
	 */
	[[nodiscard]] bool Intersects(const FPlane &plane, float &t) const noexcept {
		float denom = plane.normal.Dot(direction);
		if (sdl3::Abs(denom) < 1e-8f)
			return false; // parallel
		t = -(plane.normal.Dot(origin) + plane.d) / denom;
		return t >= 0.f;
	}

	/**
	 * Möller-Trumbore intersection with a triangle.
	 *
	 * @param v0,v1,v2  Triangle vertices.
	 * @param t   [out] distance along the ray to the intersection.
	 * @param u,v [out] Barycentric coordinates of the hit point.
	 * @returns true on intersection.
	 */
	[[nodiscard]] bool Intersects(const FVector3 &v0, const FVector3 &v1, const FVector3 &v2, float &t, float &u,
								  float &v) const noexcept {
		FVector3 e1 = v1 - v0;
		FVector3 e2 = v2 - v0;
		FVector3 h = direction.Cross(e2);
		float a = e1.Dot(h);
		if (sdl3::Abs(a) < 1e-8f)
			return false; // parallel

		float f = 1.f / a;
		FVector3 s = origin - v0;
		u = f * s.Dot(h);
		if (u < 0.f || u > 1.f)
			return false;

		FVector3 q = s.Cross(e1);
		v = f * direction.Dot(q);
		if (v < 0.f || u + v > 1.f)
			return false;

		t = f * e2.Dot(q);
		return t > 1e-8f;
	}

	/**
	 * Return a copy of this ray Transformed by `mat`.
	 *
	 * The origin is Transformed as a point and the direction as a direction
	 * (then re-normalised). Handy for casting a world ray into an object's
	 * local space using the Inverse model matrix.
	 */
	[[nodiscard]] FRay Transformed(const FMatrix4 &mat) const noexcept {
		return FRay{mat.TransformPoint(origin), mat.TransformDir(direction)};
	}
};

// ── FFrustum ────────────────────────────────────────────────────────────────────

/**
 * View frustum defined by six planes for visibility culling.
 *
 * FPlane order: left, right, bottom, top, near, far.
 */
struct FFrustum {
	FPlane planes[6];

	/**
	 * Extract the frustum from a combined view-projection matrix.
	 *
	 * Uses the Gribb-Hartmann method adapted for Vulkan clip space
	 * (Z ∈ [0, 1]).
	 *
	 * @param vp Combined view-projection matrix (column-major).
	 */
	[[nodiscard]] static FFrustum FromViewProj(const FMatrix4 &vp) noexcept {
		// Read row i as a FVector4: {m[0*4+i], m[1*4+i], m[2*4+i], m[3*4+i]}
		auto row = [&](int i) -> FVector4 {
			return {vp.m[0 * 4 + i], vp.m[1 * 4 + i], vp.m[2 * 4 + i], vp.m[3 * 4 + i]};
		};
		FVector4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

		auto makePlane = [](const FVector4 &v) -> FPlane { return {{v.x, v.y, v.z}, v.w}; };

		FFrustum f;
		f.planes[0] = makePlane(r3 + r0).Normalize(); // left   (x >= -w)
		f.planes[1] = makePlane(r3 - r0).Normalize(); // right  (x <=  w)
		f.planes[2] = makePlane(r3 + r1).Normalize(); // bottom (y >= -w)
		f.planes[3] = makePlane(r3 - r1).Normalize(); // top    (y <=  w)
		f.planes[4] = makePlane(r2).Normalize();      // near   (z >=  0, Vulkan)
		f.planes[5] = makePlane(r3 - r2).Normalize(); // far    (z <=  w)
		return f;
	}

	/**
	 * Test whether an FAABB is At least partially inside the frustum.
	 *
	 * Returns false only when the box is entirely outside one of the planes
	 * (conservative: may return true for boxes that are actually culled by
	 * the intersection of two planes).
	 *
	 * @param box FAABB to test.
	 * @returns true if the box may be visible.
	 */
	[[nodiscard]] bool Intersects(const FAABB &box) const noexcept {
		for (const auto &plane : planes) {
			// "positive vertex": the corner furthest along the plane normal
			FVector3 pv{
				plane.normal.x >= 0.f ? box.max.x : box.min.x,
				plane.normal.y >= 0.f ? box.max.y : box.min.y,
				plane.normal.z >= 0.f ? box.max.z : box.min.z,
			};
			if (plane.Distance(pv) < 0.f)
				return false;
		}
		return true;
	}
};

/** @} */ // CategoryMath3D

} // namespace math
