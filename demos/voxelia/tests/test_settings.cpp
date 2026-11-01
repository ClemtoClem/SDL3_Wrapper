// Tests — préférences de Voxelia (settings.hpp) : caméra du joueur (position,
// inclinaison), réflexions (activées, rebonds bornés à 1..16), touche
// d'échange des mains ; lecture et écriture JSON.
#include "test.hpp"

#include <SDL3/SDL.h>
#include <voxel/json.hpp>
#include <voxelia/settings.hpp>

using namespace voxelia;
namespace json = voxel::json;

TEST(Settings_CameraAndReflectionDefaults) {
	Settings s;
	s.ResetBindings();
	// Caméra : devant le visage du skin, sans décalage ni inclinaison.
	CHECK_NEAR(s.cameraForward, 0.f, 1e-6f);
	CHECK_NEAR(s.cameraUp, 0.f, 1e-6f);
	CHECK_NEAR(s.cameraSide, 0.f, 1e-6f);
	CHECK_NEAR(s.cameraTilt, 0.f, 1e-6f);
	CHECK(s.reflections);
	CHECK_EQ(s.reflectionBounces, 4);
	CHECK_EQ(Settings::REFLECTION_BOUNCES_MIN, 1);
	CHECK_EQ(Settings::REFLECTION_BOUNCES_MAX, 16);
	const KeyBinding *swap = s.Binding("swapHands");
	REQUIRE(swap != nullptr);
	CHECK_EQ(swap->keys[0], int(SDL_SCANCODE_X));
	CHECK(s.Conflicts().empty()); // la nouvelle touche ne prend celle d'aucune autre action
}

TEST(Settings_CameraAndReflectionsRoundTrip) {
	Settings s;
	s.ResetBindings();
	s.cameraForward = 0.25f;
	s.cameraUp = -0.1f;
	s.cameraSide = 0.5f;
	s.cameraTilt = -12.f;
	s.reflections = false;
	s.reflectionBounces = 9;
	Settings back;
	back.FromJson(s.ToJson());
	CHECK_NEAR(back.cameraForward, 0.25f, 1e-6f);
	CHECK_NEAR(back.cameraUp, -0.1f, 1e-6f);
	CHECK_NEAR(back.cameraSide, 0.5f, 1e-6f);
	CHECK_NEAR(back.cameraTilt, -12.f, 1e-6f);
	CHECK(!back.reflections);
	CHECK_EQ(back.reflectionBounces, 9);
	// Hors bornes : ramené à 1..16.
	auto bounces = [](int value) {
		Settings in;
		dataio::NodePtr node = in.ToJson();
		node->Set("reflectionBounces", json::MakeInt(value));
		Settings out;
		out.FromJson(node);
		return out.reflectionBounces;
	};
	CHECK_EQ(bounces(0), 1);
	CHECK_EQ(bounces(-5), 1);
	CHECK_EQ(bounces(40), 16);
	CHECK_EQ(bounces(16), 16);
}
