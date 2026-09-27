// Définitions de math/math.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "math/math.hpp"

namespace math {

// ── FVector2 ─────────────────────────────────────────────────────────────────

FVector2 FVector2::Normalize() const noexcept {
	float len = Length();
	return (len > 1e-8f) ? FVector2(*this / len) : FVector2{};
}

// ── FVector3 ─────────────────────────────────────────────────────────────────

FVector3 FVector3::Normalize() const noexcept {
	float len = Length();
	return len > 1e-8f ? (*this / len) : FVector3{};
}

// ── FVector4 ─────────────────────────────────────────────────────────────────

FVector4 FVector4::Normalize() const noexcept {
	float len = Length();
	return len > 1e-8f ? (*this / len) : FVector4{};
}

// ── FMatrix4 ─────────────────────────────────────────────────────────────────

FMatrix4 FMatrix4::RotateX(float a) noexcept {
	float c = sdl3::Cos(a), s = sdl3::Sin(a);
	FMatrix4 r = Identity();
	r.m[5] = c;
	r.m[6] = s;
	r.m[9] = -s;
	r.m[10] = c;
	return r;
}

FMatrix4 FMatrix4::RotateY(float a) noexcept {
	float c = sdl3::Cos(a), s = sdl3::Sin(a);
	FMatrix4 r = Identity();
	r.m[0] = c;
	r.m[2] = -s;
	r.m[8] = s;
	r.m[10] = c;
	return r;
}

FMatrix4 FMatrix4::RotateZ(float a) noexcept {
	float c = sdl3::Cos(a), s = sdl3::Sin(a);
	FMatrix4 r = Identity();
	r.m[0] = c;
	r.m[1] = s;
	r.m[4] = -s;
	r.m[5] = c;
	return r;
}

FMatrix4 FMatrix4::Rotate(const FVector3 &axis, float angle) noexcept {
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

FMatrix4 FMatrix4::Perspective(float fovY, float aspect, float nearZ, float farZ) noexcept {
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

FMatrix4 FMatrix4::LookAt(const FVector3 &eye, const FVector3 &Center, const FVector3 &up) noexcept {
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

FVector3 FMatrix4::TransformPoint(const FVector3 &p) const noexcept {
	return (*this * FVector4{p, 1.f}).perspDiv();
}

FMatrix4 FMatrix4::Inverse() const noexcept {
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

// ── FQuaternion ──────────────────────────────────────────────────────────────

FQuaternion FQuaternion::FromAxisAngle(const FVector3 &axis, float angle) noexcept {
	float s = sdl3::Sin(angle * 0.5f);
	float c = sdl3::Cos(angle * 0.5f);
	return {axis.x * s, axis.y * s, axis.z * s, c};
}

FQuaternion FQuaternion::FromEuler(float pitch, float yaw, float roll) noexcept {
	float cp = sdl3::Cos(pitch * 0.5f), sp = sdl3::Sin(pitch * 0.5f);
	float cy = sdl3::Cos(yaw * 0.5f), sy = sdl3::Sin(yaw * 0.5f);
	float cr = sdl3::Cos(roll * 0.5f), sr = sdl3::Sin(roll * 0.5f);
	return {cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy, sr * cp * cy - cr * sp * sy,
			cr * cp * cy + sr * sp * sy};
}

FQuaternion FQuaternion::FromTo(const FVector3 &from, const FVector3 &to) noexcept {
	FVector3 axis = from.Cross(to);
	float dot = from.Dot(to);
	// Cos(θ/2) = sqrt((1+dot)/2),  Sin(θ/2) = |axis| / (2 * Cos(θ/2))
	float w = sdl3::Sqrt((1.f + dot) * 0.5f);
	float s = (w > 1e-8f) ? 0.5f / w : 0.f;
	return FQuaternion{axis.x * s, axis.y * s, axis.z * s, w}.Normalize();
}

FQuaternion FQuaternion::Normalize() const noexcept {
	float n = Norm();
	return n > 1e-8f ? FQuaternion{x / n, y / n, z / n, w / n} : Identity();
}

FQuaternion FQuaternion::FromMatrix(const FMatrix4 &m) noexcept {
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

FVector3 FQuaternion::ToEuler() const noexcept {
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

FMatrix4 ComposeTRS(const FVector3 &translation, const FQuaternion &rotation, const FVector3 &Scale) noexcept {
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

TRS DecomposeTRS(const FMatrix4 &m) noexcept {
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

// ── FAABB ────────────────────────────────────────────────────────────────────

FAABB FAABB::Transformed(const FMatrix4 &mat) const noexcept {
	const FVector3 corners[8] = {{min.x, min.y, min.z}, {max.x, min.y, min.z}, {min.x, max.y, min.z},
								 {max.x, max.y, min.z}, {min.x, min.y, max.z}, {max.x, min.y, max.z},
								 {min.x, max.y, max.z}, {max.x, max.y, max.z}};
	FAABB result;
	for (const auto &c : corners)
		result.Expand(mat.TransformPoint(c));
	return result;
}

// ── FPlane ───────────────────────────────────────────────────────────────────

FPlane::FPlane(const FVector3 &n, const FVector3 &point) noexcept {
	normal = n.Normalize();
	d = -normal.Dot(point);
}

FPlane FPlane::fromTriangle(const FVector3 &a, const FVector3 &b, const FVector3 &c) noexcept {
	FVector3 n = (b - a).Cross(c - a).Normalize();
	return {n, -n.Dot(a)};
}

FPlane FPlane::Normalize() const noexcept {
	float len = normal.Length();
	return len > 1e-8f ? FPlane{normal / len, d / len} : *this;
}

// ── FRay ─────────────────────────────────────────────────────────────────────

float FRay::DistanceTo(const FVector3 &point) const noexcept {
	const float t = sdl3::Max((point - origin).Dot(direction), 0.f);
	return (point - At(t)).Length();
}

bool FRay::Intersects(const FAABB &box, float &tMin, float &tMax) const noexcept {
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

bool FRay::Intersects(const FPlane &plane, float &t) const noexcept {
	float denom = plane.normal.Dot(direction);
	if (sdl3::Abs(denom) < 1e-8f)
		return false; // parallel
	t = -(plane.normal.Dot(origin) + plane.d) / denom;
	return t >= 0.f;
}

bool FRay::Intersects(const FVector3 &v0, const FVector3 &v1, const FVector3 &v2, float &t, float &u, float &v) const noexcept {
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

FRay FRay::Transformed(const FMatrix4 &mat) const noexcept {
	return FRay{mat.TransformPoint(origin), mat.TransformDir(direction)};
}

// ── FFrustum ─────────────────────────────────────────────────────────────────

FFrustum FFrustum::FromViewProj(const FMatrix4 &vp) noexcept {
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

bool FFrustum::Intersects(const FAABB &box) const noexcept {
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

} // namespace math
