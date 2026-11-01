// profiler — point d'entrée (cf. profiler/app.hpp)
#include <profiler/app.hpp>

int main(int argc, char **argv) {
	std::vector<String> args;
	for (int i = 1; i < argc; ++i)
		args.push_back(String(argv[i]));
	return profiler::Run(args);
}
