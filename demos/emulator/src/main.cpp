// Point d'entrée de la démo emulator : analyse la ligne de commande et
// lance emulator::Emulator (cf. emulator/emulator.hpp).
#include <cstdio>

#include <emulator/emulator.hpp>

int main(int argc, char **argv) {
	auto options = emulator::CommandLine::Parse(argc, argv);
	if (options.IsError()) {
		std::fprintf(stderr, "%s\n", options.Error().CStr());
		return 2;
	}
	emulator::Emulator emul(std::move(options).Unwrap(), emulator::CommandLine::Rebuild(argc, argv));
	return emul.Run();
}
