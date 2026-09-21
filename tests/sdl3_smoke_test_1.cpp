#include "sdl3/sdl3.hpp"
#include <iostream>

int main() {
	using namespace sdl3;

	auto sdl = sdl3::SdlContext::Create(init_flags::VIDEO | init_flags::CAMERA | init_flags::SENSOR);
	if (!sdl) {
		std::cerr << "sdl init failed: " << sdl3::GetError().CStr() << "\n";
		return 1;
	}

	// --- camera ---
	auto cams = camera::Enumerated();
	std::cout << "cameras: " << cams.size() << "\n";
	for (auto id : cams) {
		std::cout << "  " << camera::Name(id) << " pos=" << int(camera::Position(id)) << "\n";
		auto formats = camera::SupportedFormats(id);
		std::cout << "  formats: " << formats.size() << "\n";
		auto camRes = Camera::Open(id);
		if (camRes) {
			auto &cam = camRes.Value();
			auto fmt = cam.Format();
			(void)fmt;
			auto frame = cam.AcquireFrame();
			if (frame.IsSome()) {
				std::cout << "  frame " << frame->GetWidth() << "x" << frame->GetHeight() << "\n";
			}
		}
	}
	CameraSpec spec(PixelFormat::RGBA8888, 640, 480, 30, 1);
	SDL_CameraSpec raw = spec;
	(void)raw;

	// --- sensor ---
	auto sensors = sensor::Enumerated();
	std::cout << "sensors: " << sensors.size() << "\n";
	for (auto id : sensors) {
		std::cout << "  " << sensor::NameFor(id) << " type=" << int(sensor::TypeFor(id)) << "\n";
		auto sRes = Sensor::Open(id);
		if (sRes) {
			auto &s = sRes.Value();
			auto d = s.GetData<3>();
			(void)d;
			auto view = Sensor::FromId(id);
			if (view.IsSome())
				std::cout << "  view name=" << view.Unwrap().Name() << "\n";
		}
	}
	sensor::Update();

	// --- process ---
	std::vector<String> args = {"/bin/echo", "hello-from-process"};
	auto procRes = Process::Create(args, true);
	if (!procRes) {
		std::cerr << "process create failed: " << procRes.Error().CStr() << "\n";
	} else {
		auto &proc = procRes.Value();
		auto outRes = proc.Read();
		if (outRes) {
			auto &out = outRes.Value();
			std::cout << "process output (" << out.data.size() << " bytes, exit=" << out.exitCode
					  << "): " << std::string(out.data.begin(), out.data.end());
		} else {
			std::cerr << "read failed: " << outRes.Error().CStr() << "\n";
		}
		auto stdinS = proc.StdinStream();
		auto stdoutS = proc.StdoutStream();
		(void)stdinS;
		(void)stdoutS;
		auto waited = proc.Wait(true);
		std::cout << "wait exitCode isSome=" << waited.IsSome() << "\n";
	}

	// --- storage ---
	auto storRes = Storage::OpenUser("SDL3Wrapper", "SmokeTest");
	if (!storRes) {
		std::cerr << "storage open failed: " << storRes.Error().CStr() << "\n";
	} else {
		auto &stor = storRes.Value();
		while (!stor.Ready()) { /* spin briefly */
		}
		if (stor.WriteFile(String("hello.txt"), std::vector<uint8_t>{'h', 'i'}))
			std::cerr << "error read file\n";
		auto rd = stor.ReadFile(String("hello.txt"));
		if (rd)
			std::cout << "storage read back " << rd.Value().size() << " bytes\n";
		auto info = stor.PathInfo(String("hello.txt"));
		if (info.IsSome())
			std::cout << "path size=" << info.Unwrap().size << "\n";
		auto globbed = stor.Glob("", "*.txt");
		std::cout << "glob matches: " << globbed.size() << "\n";
		if (stor.EnumerateDirectory("", [](const char *dir, const char *fname) {
			std::cout << "  entry: " << dir << fname << "\n";
			return true;
		})) {
			std::cerr << "failed to enumerate directory\n";
		}
		stor.Remove(String("hello.txt"));
	}

	// --- dialog (compile-check only, do not invoke: no display/portal in this sandbox) ---
	auto cbCheck = [](const DialogResult &r, int filter) {
		(void)r;
		(void)filter;
	};
	sdl3::dialog::Callback cb = cbCheck;
	(void)cb;

	std::cout << "smoke test done\n";
	return 0;
}
