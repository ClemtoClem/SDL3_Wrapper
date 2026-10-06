#pragma once
/**
 * data::script — l'espace de noms `math` du langage (« Script ») : une
 * enveloppe complète des mathématiques de la bibliothèque C++ ET de celles
 * du dépôt (math::, lib/include/math/math.hpp).
 *
 *   <cmath>     sin cos tan asin acos atan atan2 sinh … atanh exp exp2 expm1
 *               log log2 log10 log1p logb ilogb pow sqrt cbrt hypot fma
 *               floor ceil round trunc nearbyint fract fmod remainder fmin fmax
 *               fdim copysign nextafter ldexp frexp modf erf erfc tgamma
 *               lgamma isnan isinf isfinite isnormal signbit abs sign
 *   spéciales   beta riemann_zeta expint legendre hermite laguerre
 *               cyl_bessel_j sph_bessel (domaine vérifié : aucune exception)
 *   <numeric>   gcd lcm midpoint sum product mean (+ factorial binomial is_prime)
 *   <bit>       popcount countl_zero countr_zero bit_width has_single_bit
 *               rotl rotr byteswap bit_ceil bit_floor (sur la largeur du type)
 *   utilitaires min max clamp lerp smoothstep rad deg random random_int
 *   constantes  pi tau e phi sqrt2 … inf nan epsilon, min_i8 … max_u64,
 *               min_f32 max_f32 min_f64 max_f64 ; `physics.*` à part
 *   classes     vec2 vec3 vec4 mat4 quat aabb plane ray (math:: du dépôt),
 *               complex (std::complex), random_engine (std::mt19937_64)
 */
#include <array>
#include <bit>
#include <complex>
#include <numeric>
#include <random>

#include "../../math/math.hpp"
#include "script_std.hpp"

namespace data::script {

// ============================================================================
// Fonctions de <cmath>, <numeric> et <bit>
// ============================================================================

namespace mathlib {

using lib::Args;
using lib::As;
using lib::Fail;
using lib::TypeBuilder;

using Wide = numeric::Wide;

/// Un f32 reste un f32 ; un entier donne un f64.
[[nodiscard]] Value FloatLike(double v, const Value& model);

[[nodiscard]] Result<Value, ScriptError> RequireInteger(const Args& args, size_t i, const char* fn);

/// Bits d'un entier sur la largeur de son type (i8(-1) : 0xFF).
[[nodiscard]] uint64_t RawBits(const Value& v);

/// Valeur de `raw` (bits) dans le type de `model`.
[[nodiscard]] Value FromBits(uint64_t raw, const Value& model);

[[nodiscard]] Value FromWideInt(Wide v);

[[nodiscard]] Wide Gcd(Wide a, Wide b);

void InstallFunctions(Interpreter& vm);

} // namespace mathlib

// ============================================================================
// Classes : vecteurs, matrice, quaternion, boîte, plan, rayon (math::, la
// bibliothèque du dépôt), nombres complexes et générateur aléatoire (std::)
// ============================================================================
//
//   let v = math.vec3(1, 2, 3)        v.x, v[0], v + w, 2 * v, -v, v.dot(w), v.cross(w)
//   let m = math.mat4.translate(1, 0, 0) * math.mat4.rotate_y(math.pi_2)
//   let p = m * v                     # point transformé (vec3)
//   let q = math.quat.from_axis_angle(math.vec3(0, 1, 0), math.pi)
//   let z = math.complex(3, 4)        z.abs() == 5
//   let r = math.random_engine(42)    r.int(1, 6), r.normal(0, 1), r.shuffle(liste)
//
// Composantes en f32 (comme la bibliothèque math:: du dépôt). Sémantique de
// RÉFÉRENCE, comme les listes : `copy()` pour dupliquer avant de modifier.

namespace mathlib {

// ── Vecteurs ────────────────────────────────────────────────────────────────

struct VecObject : HostObject {

	mutable std::mutex mutex;
	int size = 3;
	float c[4] = {0, 0, 0, 0};

	void Get(float out[4]) const;
};

struct MathTypes {

	std::shared_ptr<const HostType> vec[5]; ///< indices 2, 3, 4
	std::shared_ptr<const HostType> mat4, quat, aabb, plane, ray, complex;
};

/// Les types de `math` de CET interpréteur (pour fabriquer des résultats).
[[nodiscard]] MathTypes TypesOf(Interpreter& vm);

[[nodiscard]] Value MakeVec(Interpreter& vm, int size, const float* c);

[[nodiscard]] Value MakeVec3(Interpreter& vm, const math::FVector3& v);

[[nodiscard]] Value MakeVec4(Interpreter& vm, const math::FVector4& v);

[[nodiscard]] const VecObject* AsVec(const Value& v, int size = 0);

/// Composantes d'un vecteur de taille `size` : `math.vecN`, liste `[x, y…]`
/// ou table `{x:…, y:…}`.
[[nodiscard]] Result<std::array<float, 4>, ScriptError> Components(const Value& v, int size, const char* fn);

[[nodiscard]] Result<math::FVector3, ScriptError> Vec3Of(const Value& v, const char* fn);

void DefineVector(TypeBuilder& builder, int size);

// ── Matrice 4×4 (colonnes d'abord, comme math::FMatrix4) ────────────────────

struct MatObject : HostObject {

	mutable std::mutex mutex;
	math::FMatrix4 m = math::FMatrix4::Identity();
	[[nodiscard]] math::FMatrix4 Get() const;
};

struct QuatObject : HostObject {

	mutable std::mutex mutex;
	math::FQuaternion q = math::FQuaternion::Identity();
	[[nodiscard]] math::FQuaternion Get() const;
};

[[nodiscard]] Value MakeMat(Interpreter& vm, const math::FMatrix4& m);

[[nodiscard]] Value MakeQuat(Interpreter& vm, const math::FQuaternion& q);

[[nodiscard]] const MatObject* AsMat(const Value& v);

[[nodiscard]] const QuatObject* AsQuat(const Value& v);

[[nodiscard]] Result<math::FMatrix4, ScriptError> MatOf(const Value& v, const char* fn);

[[nodiscard]] Result<math::FQuaternion, ScriptError> QuatOf(const Value& v, const char* fn);

/// Nombres (`f(x, y, z)`) ou un vec3 (`f(v)`) en un vec3.
[[nodiscard]] Result<math::FVector3, ScriptError> Vec3Args(const Args& args, const char* fn);

void DefineMat4(TypeBuilder& builder);

// ── Quaternion ──────────────────────────────────────────────────────────────

void DefineQuat(TypeBuilder& builder);

// ── Boîte englobante, plan, rayon ───────────────────────────────────────────

struct AabbObject : HostObject {

	mutable std::mutex mutex;
	math::FAABB box;
	[[nodiscard]] math::FAABB Get() const;
};

struct PlaneObject : HostObject {

	math::FPlane plane;
};

struct RayObject : HostObject {

	math::FRay ray;
};

[[nodiscard]] Value MakeAabb(Interpreter& vm, const math::FAABB& box);

[[nodiscard]] const AabbObject* AsAabb(const Value& v);

void DefineGeometry(TypeBuilder& aabb, TypeBuilder& plane, TypeBuilder& ray);

// ── Nombres complexes (std::complex<double>) ────────────────────────────────

struct ComplexObject : HostObject {

	std::complex<double> z;
};

[[nodiscard]] Option<std::complex<double>> ComplexOf(const Value& v);

void DefineComplex(TypeBuilder& builder);

// ── Générateur pseudo-aléatoire (std::mt19937_64) ───────────────────────────
//   math.random_engine(graine?) — une séquence reproductible par graine,
//   indépendante de `math.random()` (le générateur global de l'interpréteur).

struct EngineObject : HostObject {

	std::mutex mutex;
	std::mt19937_64 engine;
};

void DefineRandomEngine(TypeBuilder& builder);

void InstallClasses(Interpreter& vm);

} // namespace mathlib

/// Fonctions historiques de `math` (déplacées de l'interpréteur), puis le
/// reste de <cmath>/<numeric>/<bit> et les classes.
void InstallMathLibrary(Interpreter& vm);

} // namespace data::script
