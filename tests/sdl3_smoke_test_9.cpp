// Smoke test : hidapi (accès brut aux périphériques HID USB/Bluetooth).
// N'exige aucun périphérique HID réel : l'énumération peut être vide, et
// l'ouverture d'un device peut échouer par manque de permissions (ex: pas de
// règle udev pour /dev/hidraw*) — les deux cas sont non fatals ici.
#include "sdl3/sdl3.hpp"
#include <array>
#include <iostream>

int main() {
	using namespace sdl3;

	auto hid = HidContext::Create();
	if (!hid) {
		std::cerr << "hid init failed: " << hid.Error().CStr() << "\n";
		return 1;
	}

	std::cout << "hid device change count=" << hid::DeviceChangeCount() << "\n";

	auto devices = hid::Enumerate();
	std::cout << "hid devices: " << devices.size() << "\n";
	for (auto &d : devices) {
		std::cout << "  vid=" << d.vendorId << " pid=" << d.productId << " bus=" << int(d.busType) << " product=\""
				  << d.product.c_str() << "\"\n";

		auto devRes = HidDevice::OpenPath(d.path);
		if (!devRes) {
			std::cout << "    open failed (likely a permissions issue, non-fatal): " << devRes.Error().CStr() << "\n";
			continue;
		}
		auto &dev = devRes.Value();
		dev.SetNonBlocking(true);

		std::array<uint8_t, 64> buf{};
		int n = dev.Read(buf);
		std::cout << "    non-blocking read returned " << n << "\n";

		auto info = dev.DeviceInfo();
		if (info.IsSome())
			std::cout << "    device_info manufacturer=\"" << info.Unwrap().manufacturer.c_str() << "\"\n";
	}

	hid::BleScan(false);

	std::cout << "smoke test 9 done\n";
	return 0;
}
