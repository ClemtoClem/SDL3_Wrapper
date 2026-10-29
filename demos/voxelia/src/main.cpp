// voxelia — point d'entrée (cf. app.hpp).
#include <cstdio>

#include <voxelia/app.hpp>

int main(int argc, char **argv) {
	voxelia::Options options;
	if (Option<String> error = voxelia::ParseOptions(argc, argv, options); error.IsSome()) {
		std::fprintf(stderr, "%s\n\n%s", error.Unwrap().CStr(), voxelia::Usage());
		return 2;
	}
	voxelia::App app(std::move(options));
	return app.Run();
}
