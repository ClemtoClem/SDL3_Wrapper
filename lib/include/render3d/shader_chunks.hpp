#pragma once

namespace render3d::shader_chunks {

// Bibliothèque de fragments GLSL nommés, concaténés par ShaderBuilder pour
// composer un shader complet à la volée — l'équivalent C++ de ShaderChunk/
// ShaderLib de three.js, mais assemblés en C++ (une sélection de chaînes
// selon la config du builder) plutôt que par un résolveur de directives
// `#include` : le jeu de chunks est fixe et connu à l'avance, pas besoin de
// réimplémenter un préprocesseur.
//
// Convention de binding SPIR-V imposée par SDL_GPU (voir assets/shaders/src/
// shape2d.vert) : vertex -> set=0 échantillonneurs/stockage, set=1 uniform
// buffers ; fragment -> set=2 échantillonneurs/stockage, set=3 uniform
// buffers. Respectée par tous les chunks ci-dessous.

// ── Attributs de sommet (partagés par tous les vertex shaders) ─────────────
inline constexpr const char *VERTEX_ATTRIBUTES = R"(
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
)";

// ── InstancedMesh (M16) — mat4 par instance, locations 4-7 (4 vec4
// consécutifs, règle GLSL standard pour un attribut mat4), buffer_slot=1
// avec input_rate=INSTANCE (voir Canvas::GetOrCreatePipeline/DrawInstancedMesh)
// — utilisée à la place de PerObjectUBO.uModel quand INSTANCED_ENABLED.
inline constexpr const char *INSTANCE_ATTRIBUTES = R"(
layout(location = 4) in mat4 inInstanceMatrix;
)";

// ── SkinnedMesh (M17) — remplace VERTEX_ATTRIBUTES (pas un ajout : un maillage
// skinné n'est jamais aussi instancié dans ce port, voir le plan) : 4 indices
// + 4 poids d'os par sommet, en FLOAT4 (pas UBYTE4 — évite tout risque de
// conversion entier->flottant non éprouvée dans ce dépôt pour un format
// jamais utilisé ailleurs, voir SkinnedVertex3D dans vertex.hpp).
inline constexpr const char *VERTEX_ATTRIBUTES_SKINNED = R"(
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inBoneIndices;
layout(location = 5) in vec4 inBoneWeights;
)";

// Matrices de squelette (bone.WorldMatrix() * inverseBindMatrix par os, voir
// SkinnedMesh::UpdateSkinning() dans skinned_mesh.hpp) — storage buffer (pas
// UBO : nombre d'os variable, sans le plafond fixe qu'imposerait un UBO),
// set=0 (vertex "échantillonneurs/stockage" — voir la convention documentée
// en tête de fichier), entièrement libre avant cette phase.
inline constexpr const char *BONE_MATRIX_STORAGE_BUFFER = R"(
layout(std430, set = 0, binding = 0) readonly buffer BoneMatrices {
	mat4 uBoneMatrices[];
};
)";

inline constexpr const char *VERTEX_MAIN_UNLIT_SKINNED = R"(
void main() {
	mat4 skinMatrix = inBoneWeights.x * uBoneMatrices[int(inBoneIndices.x)] +
					  inBoneWeights.y * uBoneMatrices[int(inBoneIndices.y)] +
					  inBoneWeights.z * uBoneMatrices[int(inBoneIndices.z)] +
					  inBoneWeights.w * uBoneMatrices[int(inBoneIndices.w)];
	gl_Position = uViewProjection * skinMatrix * vec4(inPosition, 1.0);
	outColor = inColor;
	outUV = inUV;
}
)";

inline constexpr const char *VERTEX_MAIN_LIT_SKINNED = R"(
void main() {
	mat4 skinMatrix = inBoneWeights.x * uBoneMatrices[int(inBoneIndices.x)] +
					  inBoneWeights.y * uBoneMatrices[int(inBoneIndices.y)] +
					  inBoneWeights.z * uBoneMatrices[int(inBoneIndices.z)] +
					  inBoneWeights.w * uBoneMatrices[int(inBoneIndices.w)];
	// Approximation : haut-gauche 3x3 de skinMatrix, pas d'inverse-transpose
	// par sommet (même simplification qu'InstancedMesh, voir INSTANCE_ATTRIBUTES
	// ci-dessus) — correct sans mise à l'échelle non uniforme des os.
	vec4 worldPos = skinMatrix * vec4(inPosition, 1.0);
	outWorldPos = worldPos.xyz;
	outNormal = mat3(skinMatrix) * inNormal;
	outUV = inUV;
	outColor = inColor;
	gl_Position = uViewProjection * worldPos;
}
)";

// ── UBOs communs vertex (set=1) ─────────────────────────────────────────────
inline constexpr const char *PER_FRAME_UBO = R"(
layout(set = 1, binding = 0) uniform PerFrameUBO {
	mat4 uViewProjection;
};
)";

inline constexpr const char *PER_OBJECT_UBO = R"(
layout(set = 1, binding = 1) uniform PerObjectUBO {
	mat4 uModel;
	mat4 uNormalMatrix;
};
)";

// ── Corps de vertex shader ──────────────────────────────────────────────────
// UNLIT : pas besoin de la position/normale monde (pas d'éclairage).
inline constexpr const char *VERTEX_OUTPUTS_UNLIT = R"(
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUV;
)";

inline constexpr const char *VERTEX_MAIN_UNLIT = R"(
void main() {
#ifdef INSTANCED_ENABLED
	mat4 model = inInstanceMatrix;
#else
	mat4 model = uModel;
#endif
	gl_Position = uViewProjection * model * vec4(inPosition, 1.0);
	outColor = inColor;
	outUV = inUV;
}
)";

// LIT (Phong ou PBR) : position/normale monde nécessaires à l'éclairage.
inline constexpr const char *VERTEX_OUTPUTS_LIT = R"(
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec4 outColor;
)";

inline constexpr const char *VERTEX_MAIN_LIT = R"(
void main() {
#ifdef INSTANCED_ENABLED
	// Matrice normale approximée par le haut-gauche 3x3 de la matrice
	// d'instance (pas d'inverse-transpose par instance) — correct sans
	// mise à l'échelle non uniforme, même simplification que three.js
	// InstancedMesh par défaut (voir le plan, M16).
	mat4 model = inInstanceMatrix;
	mat3 normalMat = mat3(inInstanceMatrix);
#else
	mat4 model = uModel;
	mat3 normalMat = mat3(uNormalMatrix);
#endif
	vec4 worldPos = model * vec4(inPosition, 1.0);
	outWorldPos = worldPos.xyz;
	outNormal = normalMat * inNormal;
	outUV = inUV;
	outColor = inColor;
	gl_Position = uViewProjection * worldPos;
}
)";

// ── Entrées fragment (miroir des sorties vertex ci-dessus) ─────────────────
inline constexpr const char *FRAGMENT_INPUTS_UNLIT = R"(
layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUV;
)";

inline constexpr const char *FRAGMENT_INPUTS_LIT = R"(
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
)";

inline constexpr const char *FRAGMENT_OUTPUT = R"(
layout(location = 0) out vec4 outFragColor;
)";

// ── Échantillonneur albedo (set=2, fragment) ────────────────────────────────
inline constexpr const char *SAMPLER_ALBEDO = R"(
layout(set = 2, binding = 0) uniform sampler2D uAlbedo;
)";

// ── MaterialUBO — même disposition pour Unlit/Phong/PBR (les champs inutiles
// à un mode donné sont simplement ignorés par la fonction d'éclairage
// choisie ; évite de multiplier les formes d'UBO). std140 : deux vec4 (32 o)
// puis 4 float (16 o) = 48 o, sans padding manuel nécessaire.
// uUseIBL (ex-padding, M14) : 0/1, matériau PBR + Canvas::SetEnvironment actif
// (voir Canvas::End()) — remplace le terme ambiant plat par l'irradiance/
// spéculaire préfiltré réels dans ShadeSurface (LIGHTING_FUNC_PBR ci-dessous).
inline constexpr const char *MATERIAL_UBO = R"(
layout(set = 3, binding = 0) uniform MaterialUBO {
	vec4 uBaseColor;
	vec4 uEmissive;   // rgb = couleur, a = intensité
	float uRoughness;
	float uMetallic;
	float uUseTexture;
	float uUseIBL;
};
)";

// ── LightUBO — directionnelle + ambiante (binding 1), toujours présentes.
inline constexpr const char *LIGHT_UBO_BASE = R"(
layout(set = 3, binding = 1) uniform LightUBO {
	vec3 uLightDir;
	float uPadding1;
	vec4 uLightColor;
	vec4 uAmbientColor;
	vec3 uCameraPos;
	float uPadding2;
};
)";

// ── Variante avec matrices d'ombre fusionnées dans le MÊME binding=1 que
// LIGHT_UBO_BASE (utilisée à sa place, jamais en plus) : SDL_GPU limite à 4
// uniform buffers fragment par étage (déjà tous pris par Material/Light/
// Point/Spot), donc pas de binding=4 séparé possible pour ShadowMatrixUBO —
// voir le commentaire équivalent dans Canvas::End() (LightUBOWithShadow).
inline constexpr const char *LIGHT_UBO_SHADOW = R"(
layout(set = 3, binding = 1) uniform LightUBO {
	vec3 uLightDir;
	float uPadding1;
	vec4 uLightColor;
	vec4 uAmbientColor;
	vec3 uCameraPos;
	float uPadding2;
	mat4 uDirShadowMatrix;
	mat4 uSpotShadowMatrix0;
	mat4 uSpotShadowMatrix1;
	float uDirShadowEnabled;
	float uNumSpotShadows;
	float uShadowBias;
	float uPadding3;
	// Point light (cubemap de distance, M9) — pas de matrice ici, la lumière
	// dont l'indice est uPointShadowLightIndex (dans uPointLights[]) fournit
	// déjà sa position ; seule la comparaison de distance a besoin d'un biais.
	float uPointShadowEnabled;
	float uPointShadowLightIndex;
	float uPointShadowBias;
	float uPointShadowPadding;
};
)";

// ── Structures + tableaux de lumières ponctuelles/spot (binding 2 et 3),
// tailles injectées via #define NUM_POINT_LIGHTS / NUM_SPOT_LIGHTS en tête de
// source par ShaderBuilder — GLSL n'autorise pas les tableaux de taille 0,
// donc ces chunks ne sont inclus que si le compte correspondant est > 0.
inline constexpr const char *POINT_LIGHT_ARRAY = R"(
struct PointLightData {
	vec3 position;
	float distanceVal;
	vec3 color;
	float decay;
};
layout(set = 3, binding = 2) uniform PointLightUBO {
	PointLightData uPointLights[NUM_POINT_LIGHTS];
};
)";

inline constexpr const char *SPOT_LIGHT_ARRAY = R"(
struct SpotLightData {
	vec3 position;
	float distanceVal;
	vec3 direction;
	float angleCos;
	vec3 color;
	float decay;
	float penumbra;
	float pad0, pad1, pad2;
};
layout(set = 3, binding = 3) uniform SpotLightUBO {
	SpotLightData uSpotLights[NUM_SPOT_LIGHTS];
};
)";

// ── Atténuation partagée point/spot (three.js : physicalAttenuation) ───────
inline constexpr const char *LIGHT_ATTENUATION_FUNC = R"(
float PunctualAttenuation(float dist, float cutoffDistance, float decayExp) {
	float denom = pow(dist, decayExp);
	if (cutoffDistance <= 0.0)
		return 1.0 / max(denom, 0.0001);
	float falloff = clamp(1.0 - pow(dist / cutoffDistance, 4.0), 0.0, 1.0);
	return (falloff * falloff) / max(denom, 0.0001);
}
)";

// ── Stub d'ombre — utilisé quand SHADOW_ENABLED n'est pas défini (aucune
// lumière de la scène ne projette d'ombre) : toujours "non ombré" (1.0),
// nommé plutôt que silencieusement absent pour que .Shadow(n) reste visible
// dans le code généré même désactivé.
inline constexpr const char *SHADOW_STUB_FUNC = R"(
float ComputeShadowFactor(vec3 worldPos) {
	return 1.0;
}
)";

// ── Shadow mapping réel (SHADOW_ENABLED) — directionnelle (ortho) + jusqu'à
// MAX_SPOT_SHADOWS=2 spots (perspective), voir light.hpp pour les matrices
// vue-projection côté CPU (DirectionalLight/SpotLight::ShadowViewProjection)
// et shadow.hpp pour la création des depth textures/sampler de comparaison.
// Les matrices/compteurs vivent dans LIGHT_UBO_SHADOW (binding=1, ci-dessus)
// plutôt qu'un binding dédié — SDL_GPU limite à 4 uniform buffers fragment.
// uDirShadowEnabled/uNumSpotShadows sont des uniforms (pas des #define) :
// permet à Canvas de réutiliser le même pipeline "shadow-aware" que la
// lumière projetant une ombre change d'une frame à l'autre, sans recompiler.

// ── Échantillonneurs de comparaison matérielle (set=2, bindings 1-3) — la
// taille de la depth texture (SHADOW_MAP_SIZE) est injectée en #define par
// ShaderBuilder, doit rester synchronisée avec la résolution réellement
// créée par Canvas (voir shadow.hpp/canvas.hpp — même duplication assumée
// que ResolveShadowDepthFormat, pas de dépendance croisée entre modules).
inline constexpr const char *SAMPLER_SHADOW_MAPS = R"(
layout(set = 2, binding = 1) uniform sampler2DShadow uDirShadowMap;
layout(set = 2, binding = 2) uniform sampler2DShadow uSpotShadowMap0;
layout(set = 2, binding = 3) uniform sampler2DShadow uSpotShadowMap1;
// Cubemap de distance (pas de comparaison matérielle — voir shadow.hpp,
// technique three.js), binding=4, toujours déclarée avec les autres shadow
// maps (uPointShadowEnabled décide à l'exécution si elle est réellement
// utilisée, même logique que uDirShadowEnabled/uNumSpotShadows).
layout(set = 2, binding = 4) uniform samplerCube uPointShadowMap;
)";

// ── IBL (M14) — irradiance diffuse + spéculaire préfiltré + BRDF LUT (voir
// environment.hpp). Le binding de départ dépend de la présence ou non des 4
// échantillonneurs d'ombre déclarés juste au-dessus (1-4) : ShaderBuilder
// choisit la variante adaptée plutôt que de risquer une collision de binding.
inline constexpr const char *SAMPLER_IBL_NO_SHADOWS = R"(
layout(set = 2, binding = 1) uniform samplerCube uIrradianceMap;
layout(set = 2, binding = 2) uniform samplerCube uPrefilteredMap;
layout(set = 2, binding = 3) uniform sampler2D uBrdfLut;
)";
inline constexpr const char *SAMPLER_IBL_WITH_SHADOWS = R"(
layout(set = 2, binding = 5) uniform samplerCube uIrradianceMap;
layout(set = 2, binding = 6) uniform samplerCube uPrefilteredMap;
layout(set = 2, binding = 7) uniform sampler2D uBrdfLut;
)";

inline constexpr const char *SHADOW_REAL_FUNC = R"(
float SampleShadowPCF(sampler2DShadow shadowMap, vec3 shadowCoord) {
	if (shadowCoord.z > 1.0 || shadowCoord.z < 0.0)
		return 1.0;
	float texel = 1.0 / SHADOW_MAP_SIZE;
	float sum = 0.0;
	for (int x = -1; x <= 1; ++x) {
		for (int y = -1; y <= 1; ++y) {
			sum += texture(shadowMap, shadowCoord + vec3(float(x) * texel, float(y) * texel, -uShadowBias));
		}
	}
	return sum / 9.0;
}

float ComputeShadowFactor(vec3 worldPos) {
	if (uDirShadowEnabled < 0.5)
		return 1.0;
	vec4 clip = uDirShadowMatrix * vec4(worldPos, 1.0);
	vec3 ndc = clip.xyz / clip.w;
	return SampleShadowPCF(uDirShadowMap, vec3(ndc.xy * 0.5 + 0.5, ndc.z));
}

// Deux branches explicites plutôt qu'une sélection dynamique de sampler
// (ternaire sur un type opaque) : évite de dépendre du support de ce
// pattern par la cross-compilation SPIR-V -> MSL, jamais éprouvé ici.
float ComputeSpotShadowFactor(int spotIndex, vec3 worldPos) {
	if (spotIndex == 0) {
		if (uNumSpotShadows < 1.0)
			return 1.0;
		vec4 clip = uSpotShadowMatrix0 * vec4(worldPos, 1.0);
		vec3 ndc = clip.xyz / clip.w;
		return SampleShadowPCF(uSpotShadowMap0, vec3(ndc.xy * 0.5 + 0.5, ndc.z));
	} else {
		if (uNumSpotShadows < 2.0)
			return 1.0;
		vec4 clip = uSpotShadowMatrix1 * vec4(worldPos, 1.0);
		vec3 ndc = clip.xyz / clip.w;
		return SampleShadowPCF(uSpotShadowMap1, vec3(ndc.xy * 0.5 + 0.5, ndc.z));
	}
}

// `toLight` = position lumière - position fragment (déjà calculé par
// l'appelant dans la boucle des lumières ponctuelles) ; `dist` sa longueur.
// La cubemap est échantillonnée dans la direction opposée (fragment vu
// depuis la lumière), cohérente avec les 6 passes de rendu — voir
// PointLight::ShadowViewProjection (light.hpp) et shadow.hpp.
float ComputePointShadowFactor(vec3 toLight, float dist) {
	if (uPointShadowEnabled < 0.5)
		return 1.0;
	float storedDist = texture(uPointShadowMap, -toLight).r;
	return (dist - uPointShadowBias <= storedDist) ? 1.0 : 0.0;
}
)";

// ── Éclairage Phong (Lambert diffus + spéculaire Blinn-Phong) — reprend
// exactement la formule de assets/shaders/src/mesh_phong.frag, étendue aux
// lumières ponctuelles/spot le cas échéant.
inline constexpr const char *LIGHTING_FUNC_PHONG = R"(
vec3 ShadeSurface(vec3 albedoRgb, vec3 N, vec3 V, float shadow) {
	vec3 L0 = normalize(-uLightDir);
	float diffuse0 = max(dot(N, L0), 0.0);
	float shininess = mix(64.0, 4.0, uRoughness);
	vec3 H0 = normalize(L0 + V);
	float specular0 = pow(max(dot(N, H0), 0.0), shininess) * (1.0 - uRoughness);

	vec3 color = albedoRgb * uAmbientColor.rgb;
	color += (albedoRgb * uLightColor.rgb * diffuse0 + uLightColor.rgb * specular0) * shadow;

#if NUM_POINT_LIGHTS > 0
	for (int i = 0; i < NUM_POINT_LIGHTS; ++i) {
		vec3 toLight = uPointLights[i].position - inWorldPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 0.0001);
		float atten = PunctualAttenuation(dist, uPointLights[i].distanceVal, uPointLights[i].decay);
		float diffuse = max(dot(N, L), 0.0) * atten;
		vec3 H = normalize(L + V);
		float specular = pow(max(dot(N, H), 0.0), shininess) * (1.0 - uRoughness) * atten;
		float pointShadowFactor = 1.0;
#ifdef SHADOW_ENABLED
		if (i == int(uPointShadowLightIndex))
			pointShadowFactor = ComputePointShadowFactor(toLight, dist);
#endif
		color += (albedoRgb * uPointLights[i].color * diffuse + uPointLights[i].color * specular) * pointShadowFactor;
	}
#endif

#if NUM_SPOT_LIGHTS > 0
	for (int i = 0; i < NUM_SPOT_LIGHTS; ++i) {
		vec3 toLight = uSpotLights[i].position - inWorldPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 0.0001);
		float atten = PunctualAttenuation(dist, uSpotLights[i].distanceVal, uSpotLights[i].decay);
		float cosAngle = dot(L, normalize(-uSpotLights[i].direction));
		float spotFalloff = smoothstep(uSpotLights[i].angleCos,
									   mix(uSpotLights[i].angleCos, 1.0, uSpotLights[i].penumbra), cosAngle);
		atten *= spotFalloff;
		float diffuse = max(dot(N, L), 0.0) * atten;
		vec3 H = normalize(L + V);
		float specular = pow(max(dot(N, H), 0.0), shininess) * (1.0 - uRoughness) * atten;
		float spotShadowFactor = 1.0;
#ifdef SHADOW_ENABLED
		if (i < 2)
			spotShadowFactor = ComputeSpotShadowFactor(i, inWorldPos);
#endif
		color += (albedoRgb * uSpotLights[i].color * diffuse + uSpotLights[i].color * specular) * spotShadowFactor;
	}
#endif

	return color;
}
)";

// ── Éclairage PBR metallic-roughness (Cook-Torrance, GGX / Smith / Schlick),
// lumières directes uniquement (pas d'IBL — hors périmètre de cette phase).
inline constexpr const char *LIGHTING_FUNC_PBR = R"(
const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
	return a2 / max(PI * denom * denom, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	return NdotV / max(NdotV * (1.0 - k) + k, 0.0001);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
	return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Variante "roughness-aware" (Sébastien Lagarde) utilisée pour l'IBL (M14) :
// clamp par (1-roughness) au lieu de 1.0 — évite un halo de Fresnel trop
// marqué sur les surfaces rugueuses, différent de FresnelSchlick ci-dessus
// (éclairage direct), les deux ne sont pas interchangeables.
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 PbrRadiance(vec3 albedo, vec3 N, vec3 V, vec3 L, vec3 radiance, float roughness, float metallic, vec3 F0) {
	vec3 H = normalize(V + L);
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	if (NdotL <= 0.0)
		return vec3(0.0);

	float D = DistributionGGX(N, H, roughness);
	float G = GeometrySmith(NdotV, NdotL, roughness);
	vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

	vec3 numerator = D * G * F;
	float denominator = 4.0 * NdotV * NdotL;
	vec3 specular = numerator / max(denominator, 0.0001);

	vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
	return (kD * albedo / PI + specular) * radiance * NdotL;
}

vec3 ShadeSurface(vec3 albedoRgb, vec3 N, vec3 V, float shadow) {
	float roughness = clamp(uRoughness, 0.045, 1.0);
	float metallic = clamp(uMetallic, 0.0, 1.0);
	vec3 F0 = mix(vec3(0.04), albedoRgb, metallic);

	vec3 color;
#ifdef IBL_ENABLED
	if (uUseIBL > 0.5) {
		// Split-sum (Karis/Epic) : irradiance diffuse + spéculaire préfiltré
		// (mip choisi par roughness) x (F0*scale+bias, voir la BRDF LUT,
		// environment.hpp) — remplace le terme ambiant plat ci-dessous.
		vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
		vec3 kD = (1.0 - F) * (1.0 - metallic);
		vec3 irradiance = texture(uIrradianceMap, N).rgb;
		vec3 diffuseIBL = irradiance * albedoRgb;
		const float MAX_REFLECTION_LOD = 4.0;
		vec3 prefilteredColor = textureLod(uPrefilteredMap, reflect(-V, N), roughness * MAX_REFLECTION_LOD).rgb;
		vec2 brdf = texture(uBrdfLut, vec2(max(dot(N, V), 0.0), roughness)).rg;
		vec3 specularIBL = prefilteredColor * (F0 * brdf.x + brdf.y);
		color = kD * diffuseIBL + specularIBL;
	} else {
		color = albedoRgb * uAmbientColor.rgb * (1.0 - metallic);
	}
#else
	color = albedoRgb * uAmbientColor.rgb * (1.0 - metallic);
#endif

	vec3 L0 = normalize(-uLightDir);
	color += PbrRadiance(albedoRgb, N, V, L0, uLightColor.rgb, roughness, metallic, F0) * shadow;

#if NUM_POINT_LIGHTS > 0
	for (int i = 0; i < NUM_POINT_LIGHTS; ++i) {
		vec3 toLight = uPointLights[i].position - inWorldPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 0.0001);
		float atten = PunctualAttenuation(dist, uPointLights[i].distanceVal, uPointLights[i].decay);
		float pointShadowFactor = 1.0;
#ifdef SHADOW_ENABLED
		if (i == int(uPointShadowLightIndex))
			pointShadowFactor = ComputePointShadowFactor(toLight, dist);
#endif
		color += PbrRadiance(albedoRgb, N, V, L, uPointLights[i].color * atten, roughness, metallic, F0) * pointShadowFactor;
	}
#endif

#if NUM_SPOT_LIGHTS > 0
	for (int i = 0; i < NUM_SPOT_LIGHTS; ++i) {
		vec3 toLight = uSpotLights[i].position - inWorldPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 0.0001);
		float atten = PunctualAttenuation(dist, uSpotLights[i].distanceVal, uSpotLights[i].decay);
		float cosAngle = dot(L, normalize(-uSpotLights[i].direction));
		float spotFalloff = smoothstep(uSpotLights[i].angleCos,
									   mix(uSpotLights[i].angleCos, 1.0, uSpotLights[i].penumbra), cosAngle);
		atten *= spotFalloff;
		float spotShadowFactor = 1.0;
#ifdef SHADOW_ENABLED
		if (i < 2)
			spotShadowFactor = ComputeSpotShadowFactor(i, inWorldPos);
#endif
		color += PbrRadiance(albedoRgb, N, V, L, uSpotLights[i].color * atten, roughness, metallic, F0) * spotShadowFactor;
	}
#endif

	return color + uEmissive.rgb * uEmissive.a;
}
)";

// ── Corps de fragment shader — commun à Phong/PBR (délègue le calcul propre
// au modèle à ShadeSurface(), défini par le chunk d'éclairage sélectionné).
inline constexpr const char *FRAGMENT_MAIN_LIT = R"(
void main() {
	vec3 N = normalize(inNormal);
	vec3 V = normalize(uCameraPos - inWorldPos);

	vec4 albedo = uBaseColor * inColor;
	if (uUseTexture > 0.5)
		albedo *= texture(uAlbedo, inUV);

	float shadow = ComputeShadowFactor(inWorldPos);
	vec3 color = ShadeSurface(albedo.rgb, N, V, shadow);
	outFragColor = vec4(color, albedo.a);
}
)";

inline constexpr const char *FRAGMENT_MAIN_UNLIT = R"(
void main() {
	vec4 albedo = uBaseColor * inColor;
	if (uUseTexture > 0.5)
		albedo *= texture(uAlbedo, inUV);
	outFragColor = albedo;
}
)";

} // namespace render3d::shader_chunks
