// Smoke test : SDL3_net (TCP + UDP loopback), SDL3 GPU (device creation, best-effort —
// no GPU backend is required to pass), and dialog (compile/link check only, not invoked:
// it needs a display portal and would pop a real dialog, unsuitable for an automated test).
#include "sdl3/sdl3.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>

static bool WaitUntil(std::function<bool()> pred, int attempts = 50, int stepMs = 20) {
	for (int i = 0; i < attempts; ++i) {
		if (pred())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
	}
	return pred();
}

int main() {
	setenv("SDL_VIDEODRIVER", "dummy", 0);

	using namespace sdl3;

	auto sdl = sdl3::SdlContext::Create(init_flags::VIDEO);
	if (!sdl) {
		std::cerr << "sdl init failed: " << sdl3::GetError().CStr() << "\n";
		return 1;
	}

	// --- SDL3_net : boucle locale TCP + UDP ---
	auto net = NetContext::Create();
	if (!net) {
		std::cerr << "net init failed: " << net.Error().CStr() << "\n";
		return 1;
	}

	constexpr uint16_t TCP_PORT = 47812;
	auto serverRes = TcpServer::Listen(TCP_PORT);
	if (!serverRes) {
		std::cerr << "listen failed: " << serverRes.Error().CStr() << "\n";
		return 1;
	}
	auto &server = serverRes.Value();

	auto addrRes = IpAddress::Resolve("127.0.0.1");
	if (!addrRes) {
		std::cerr << "resolve failed: " << addrRes.Error().CStr() << "\n";
		return 1;
	}
	auto &addr = addrRes.Value();
	addr.Wait(2000);

	auto clientRes = TcpSocket::Connect(addr, TCP_PORT);
	if (!clientRes) {
		std::cerr << "connect failed: " << clientRes.Error().CStr() << "\n";
		return 1;
	}
	auto &client = clientRes.Value();
	client.WaitConnected(2000);

	Option<TcpSocket> serverSide = NONE;
	WaitUntil([&] {
		serverSide = server.Accept();
		return serverSide.IsSome();
	});
	if (serverSide.IsNone()) {
		std::cerr << "tcp accept timed out\n";
		return 1;
	}

	const char MSG[] = "ping";
	client.Send(MSG, int(sizeof(MSG)));

	char buf[64] = {};
	int received = 0;
	WaitUntil([&] {
		received = serverSide->Receive(buf, sizeof(buf));
		return received > 0;
	});
	std::cout << "tcp: server received " << received << " bytes: " << buf << "\n";

	auto udpServerRes = UdpSocket::Open(47813);
	auto udpClientRes = UdpSocket::Open(0);
	if (udpServerRes && udpClientRes) {
		auto &udpServer = udpServerRes.Value();
		auto &udpClient = udpClientRes.Value();
		udpClient.Send(addr, 47813, MSG, int(sizeof(MSG)));

		Option<ReceivedDatagram> dgram = NONE;
		WaitUntil([&] {
			dgram = udpServer.Receive();
			return dgram.IsSome();
		});
		if (dgram.IsSome())
			std::cout << "udp: received " << dgram->data.size() << " bytes from " << dgram->senderAddr.c_str() << "\n";
		else
			std::cerr << "udp receive timed out (non-fatal)\n";
	} else {
		std::cerr << "udp socket open failed (non-fatal)\n";
	}

	// --- SDL GPU : la création peut échouer sans backend matériel — non bloquant ---
	auto gpuRes = GpuDevice::Create(
		SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB, false, nullptr);
	if (gpuRes) {
		auto &gpu = gpuRes.Value();
		std::cout << "gpu device driver=" << gpu.Driver() << "\n";
	} else {
		std::cout << "gpu device creation failed (expected without a GPU backend): " << gpuRes.Error().CStr() << "\n";
	}

	// --- Dialog : vérifie juste que l'API compile/lie correctement ---
	dialog::Callback cb = [](const DialogResult &r, int filter) {
		(void)r;
		(void)filter;
	};
	(void)cb;
	DialogFilter filter{"Images", "png;jpg"};
	(void)filter;

	std::cout << "smoke test 6 done\n";
	return 0;
}
