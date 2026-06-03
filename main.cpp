#include <windows.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include <includes.hpp>

import misc.number;
import game.server;

namespace craftbuild {
	static std::atomic<bool> keep_running = true;

	BOOL WINAPI server_console_handler(DWORD signal) {
		if (signal == CTRL_C_EVENT or signal == CTRL_BREAK_EVENT or signal == CTRL_CLOSE_EVENT) {
			keep_running.store(false, std::memory_order_release);
			return TRUE;
		}
		return FALSE;
	}
}

int main(int argc, char** argv) {
	using namespace craftbuild;

	uint16 udp_port = 5000;
	uint16 tcp_port = 8080;
	if (argc >= 2) udp_port = static_cast<uint16>(std::stoi(argv[1]));
	if (argc >= 3) tcp_port = static_cast<uint16>(std::stoi(argv[2]));

	SetConsoleCtrlHandler(server_console_handler, TRUE);

	UDPServer udp_server;
	TCPServer tcp_server;

	if (not udp_server.start(udp_port)) return 1;
	if (not tcp_server.start(tcp_port)) {
		udp_server.stop();
		return 1;
	}

	std::cout << "Craftbuild server is running. Ctrl+C to stop.\n";
	while (keep_running.load(std::memory_order_acquire)) {
		// Main server loop stays small: socket threads do listening, this tick is
		// for cheap periodic server-side work that should not block networking.
		udp_server.process();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	std::cout << "Stopping Craftbuild server...\n";
	tcp_server.stop();
	udp_server.stop();
	return 0;
}
