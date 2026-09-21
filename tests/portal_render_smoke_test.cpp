// Smoke test : render3d::Portal / RenderPortalRecursive (lib/include/
// render3d/portal.hpp) — M30 (Phase 9 du plan). Two kinds of verification,
// matching this repo's established GPU-pixel-readback rigor
// (tests/render3d_smoke_test.cpp's InstancedMeshPixelReadbackShowsTwoDistinct
// Instances / tests/offscreen_smoke_test.cpp):
//
// 1. CPU-only closed-form check: Portal::delta/deltaInv are genuine matrix
//    inverses of each other for a non-trivial (translated + rotated) portal
//    pair — catches a whole class of transposition/inversion-order bugs
//    immediately without needing a GPU.
// 2. GPU pixel-readback: a short "corridor" scene with two portals (A at the
//    world origin, B translated on X) and a distinctly-colored marker cube
//    placed ONLY near B, invisible from the main camera directly. Looking
//    through portal A (RenderPortalRecursive, depth=0/maxDepth=0 — no
//    recursion needed to prove the core mechanism) must reveal the marker's
//    exact color on portal A's own quad when the main scene is rendered
//    normally afterward — confirms the recursive render genuinely shows
//    "the other side," not a copy of the local scene or a black/empty
//    texture (the single most important check in this whole milestone).
#define USE_TEST
#include "core/test.hpp"
#include "render3d/portal.hpp"
#include "sdl3/sdl3.hpp"
#include <cmath>

using namespace sdl3;
using namespace render3d;
using namespace math;

namespace {
constexpr GpuTextureFormat COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr GpuTextureFormat DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

/// Flat quad (2 triangles) in the local XY plane at z=0, normal +Z (this
/// codebase's "front face" convention — see Portal::WorldPlaneNormal's own
/// doc comment) spanning [-halfExtent, halfExtent] on both axes.
Mesh MakeQuad(float halfExtent) {
	std::vector<Vertex3D> verts = {
		{{-halfExtent, -halfExtent, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}, Color::WHITE()},
		{{halfExtent, -halfExtent, 0.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}, Color::WHITE()},
		{{halfExtent, halfExtent, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f}, Color::WHITE()},
		{{-halfExtent, halfExtent, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f}, Color::WHITE()},
	};
	std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
	return Mesh(verts, idx);
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────
// 1. CPU-only: delta * deltaInv ~= Identity for a translated+rotated portal
//    pair, and the symmetric relationship documented in portal.hpp's file
//    header (linkedPortal.deltaInv == portal.delta) also holds.
// ─────────────────────────────────────────────────────────────────────────
TEST(Portal, DeltaAndDeltaInvAreExactInversesForNonTrivialPair) {
	Object3D nodeA, nodeB;
	nodeA.SetPosition({1.f, 2.f, -3.f});
	nodeA.SetRotation(FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, 0.7f));
	nodeB.SetPosition({-5.f, 0.5f, 8.f});
	nodeB.SetRotation(FQuaternion::FromAxisAngle({1.f, 0.f, 0.f}, -1.1f));

	Portal a, b;
	a.node = &nodeA;
	a.linkedPortal = &b;
	b.node = &nodeB;
	b.linkedPortal = &a;
	a.UpdateDelta();
	b.UpdateDelta();

	FMatrix4 product = a.delta * a.deltaInv;
	FMatrix4 identity = FMatrix4::Identity();
	for (int i = 0; i < 16; ++i)
		EXPECT_TRUE(std::abs(product.m[i] - identity.m[i]) < 1e-3f);

	FMatrix4 product2 = a.deltaInv * a.delta;
	for (int i = 0; i < 16; ++i)
		EXPECT_TRUE(std::abs(product2.m[i] - identity.m[i]) < 1e-3f);

	// b.delta/deltaInv were computed independently (b.UpdateDelta() reads
	// nodeB/nodeA's world matrices itself) yet must land on the exact
	// algebraic mirror of a's — see portal.hpp's file header derivation.
	for (int i = 0; i < 16; ++i) {
		EXPECT_TRUE(std::abs(b.deltaInv.m[i] - a.delta.m[i]) < 1e-4f);
		EXPECT_TRUE(std::abs(b.delta.m[i] - a.deltaInv.m[i]) < 1e-4f);
	}
}

// ─────────────────────────────────────────────────────────────────────────
// 2. GPU pixel-readback: looking through portal A reveals the marker cube
//    that sits only near portal B.
// ─────────────────────────────────────────────────────────────────────────
TEST(PortalRender, LookingThroughPortalRevealsDistinctFarSideColor) {
	auto windowResult = Window::Create(String("portal_render_smoke_test"), 64, 64, 0);
	ASSERT_TRUE(windowResult.IsOk());
	Window window = std::move(windowResult.Value());

	auto canvasResult = Canvas::Create(window, 64, 64);
	ASSERT_TRUE(canvasResult.IsOk());
	Canvas canvas = std::move(canvasResult.Value());

	// Zero-intensity directional + full white ambient: isolates exactly
	// `albedo.rgb` in the lit/textured shader path (same trick as
	// tests/offscreen_smoke_test.cpp's UploadSurfaceToGpuTextureRoundTrips
	// ExactColors — the UNLIT/basic shader never samples a texture at all,
	// per that test's own comment, so portal A's quad — which must display
	// a SAMPLED texture — needs the lit path with lighting neutralised).
	DirectionalLight noLight;
	noLight.intensity = 0.f;
	AmbientLight fullAmbient;
	fullAmbient.color = Color::WHITE();
	fullAmbient.intensity = 1.f;
	canvas.SetLighting(noLight, fullAmbient);

	Object3D scene;

	// Portal A: at the world origin, quad half-extent 4 comfortably exceeds
	// the main camera's half-frustum-width at distance 5 (5*tan(30 deg) ~=
	// 2.887, same "fills the whole viewport" technique as tests/
	// offscreen_smoke_test.cpp's FlatUnlitCubeFillsTargetWithExactColor).
	auto &portalANode =
		static_cast<Shape &>(scene.Add(std::make_unique<Shape>(MakeQuad(4.f), Material::Default())));
	portalANode.Materials()[0].doubleSided = true;

	// Portal B: translated +20 on X, otherwise identical pose (pure-
	// translation delta — the simplest, most legible corridor). A plain
	// Object3D (no Shape/geometry) is enough: Portal only needs its
	// WorldMatrix() for WorldPlanePos()/WorldPlaneNormal(), and this test
	// uses maxDepth=0 so the Shape-bounds visibility check never runs.
	auto &portalBNode = scene.Add(std::make_unique<Object3D>());
	portalBNode.SetPosition({20.f, 0.f, 0.f});

	// Marker cube: bright, distinct green, placed well beyond portal B's
	// plane (z=2..12, clear of the z=0 clip plane — see the file's own
	// reasoning for why it must be beyond, not at, B's position) and sized
	// (half-extent 5) to fill the virtual camera's viewport at its distance
	// (7 units from the virtual camera to the cube's front face; required
	// half-extent to fill > 7*tan(30 deg) ~= 4.04, 5 gives margin) — same
	// "fills the whole viewport with one exact color" technique as above,
	// just applied to the RECURSIVE render instead of the main one.
	Color markerColor(30, 220, 60, 255);
	auto &marker =
		static_cast<Shape &>(scene.Add(std::make_unique<Shape>(Mesh::Cube(10.f), Material::Unlit(markerColor))));
	marker.SetPosition({20.f, 0.f, 7.f});

	// A background object clearly visible to the MAIN camera directly (NOT
	// through the portal) with a color distinct from both the marker green
	// and the background clear color — confirms portal A's quad shows the
	// portal content specifically, not merely "whatever was already there."
	Color foregroundColor(200, 40, 220, 255); // magenta-ish, unlike marker green
	auto &foreground = static_cast<Shape &>(
		scene.Add(std::make_unique<Shape>(Mesh::Cube(1.f), Material::Unlit(foregroundColor))));
	foreground.SetPosition({0.f, 0.f, -8.f}); // behind the main camera's portal view, off to the side of nothing else

	render3d::Camera mainCam;
	mainCam.position = {0.f, 0.f, -5.f};
	mainCam.target = {0.f, 0.f, 0.f};
	mainCam.up = {0.f, 1.f, 0.f};
	mainCam.aspect = 1.f; // matches the square targets below; explicit-vp overload does NOT auto-override this

	Portal portalA, portalB;
	portalA.node = &portalANode;
	portalA.linkedPortal = &portalB;
	portalB.node = &portalBNode;
	portalB.linkedPortal = &portalA;
	portalA.UpdateDelta();
	portalB.UpdateDelta();

	OffscreenTarget portalTarget;
	auto portalSized = portalTarget.EnsureSize(canvas.Device(), 64, 64, COLOR_FORMAT, DEPTH_FORMAT);
	ASSERT_TRUE(portalSized.IsOk());

	// depth=0, maxDepth=0: no recursion attempted (portal B is never itself
	// looked "through" here) — isolates the core virtual-camera/oblique-clip
	// mechanism from the recursive-compositing machinery, tested separately
	// in spirit by the CPU-only delta/deltaInv test above (which confirms
	// the exact algebraic relationship the recursion depends on).
	RenderPortalRecursive(canvas, scene, mainCam, portalA, /*depth*/ 0, /*maxDepth*/ 0, portalTarget);

	// portalANode's material should now be pointing at portalTarget's color
	// texture (RenderPortalRecursive's documented postcondition) — render
	// the FULL scene normally with the main camera and read back pixels.
	OffscreenTarget mainTarget;
	auto mainSized = mainTarget.EnsureSize(canvas.Device(), 64, 64, COLOR_FORMAT, DEPTH_FORMAT);
	ASSERT_TRUE(mainSized.IsOk());

	std::vector<uint8_t> pixels;
	auto rendered = RenderObjectToTexture(canvas, scene, mainCam, mainTarget, pixels);
	ASSERT_TRUE(rendered.IsOk());
	ASSERT_EQ(pixels.size(), size_t(64 * 64 * 4));

	auto checkPixelIsMarkerGreen = [&](uint32_t x, uint32_t y) {
		size_t idx = (size_t(y) * 64 + x) * 4;
		EXPECT_TRUE(std::abs(int(pixels[idx + 0]) - int(markerColor.r)) <= 6);
		EXPECT_TRUE(std::abs(int(pixels[idx + 1]) - int(markerColor.g)) <= 6);
		EXPECT_TRUE(std::abs(int(pixels[idx + 2]) - int(markerColor.b)) <= 6);
	};
	// Portal A's quad fills the entire 64x64 target (see MakeQuad(4.f)'s own
	// sizing comment above) — every pixel, corners included, must show the
	// marker's exact green, not the magenta foreground cube, not black/empty.
	checkPixelIsMarkerGreen(32, 32); // center
	checkPixelIsMarkerGreen(1, 1);   // top-left corner
	checkPixelIsMarkerGreen(62, 62); // bottom-right corner
	checkPixelIsMarkerGreen(62, 1);  // top-right corner
	checkPixelIsMarkerGreen(1, 62);  // bottom-left corner

	// Explicit negative control: NOT the foreground magenta anywhere.
	for (uint32_t y = 0; y < 64; y += 16) {
		for (uint32_t x = 0; x < 64; x += 16) {
			size_t idx = (size_t(y) * 64 + x) * 4;
			bool looksMagenta =
				std::abs(int(pixels[idx + 0]) - int(foregroundColor.r)) <= 6 &&
				std::abs(int(pixels[idx + 1]) - int(foregroundColor.g)) <= 6 &&
				std::abs(int(pixels[idx + 2]) - int(foregroundColor.b)) <= 6;
			EXPECT_TRUE(!looksMagenta);
		}
	}
}

int main() {
	return RUN_ALL_TESTS();
}
