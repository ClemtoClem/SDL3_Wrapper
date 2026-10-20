// voxelgame — point d'entrée (cf. app.hpp).
#include <cstdio>

#include <voxelgame/app.hpp>

int main(int argc, char **argv) {
	voxelgame::Options options;
	if (Option<String> error = voxelgame::ParseOptions(argc, argv, options); error.IsSome()) {
		std::fprintf(stderr, "%s\n\n%s", error.Unwrap().CStr(), voxelgame::Usage());
		return 2;
	}
	voxelgame::App app(std::move(options));
	return app.Run();
}
