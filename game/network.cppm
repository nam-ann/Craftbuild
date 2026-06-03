module;

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#undef ERROR

#include <includes.hpp>

export module game.network;

import misc.str;
import misc.number;
import misc.format;
import game.logger;

export namespace craftbuild {
    struct Message {
        Str content;
		std::vector<std::string> arguments;
    };

    struct SendQueue {
        std::vector<Message> msg;

        none store(const Message& message) {
            msg.push_back(message);
        }

        none send(SOCKET socket) {
			for (const auto& message : msg) {
                std::string arg = message.content.std_str() + '\2';
				for (const auto& E : message.arguments) arg += E + '\1';
				uint64 arg_size = arg.size();
				::send(socket, reinterpret_cast<char*>(&arg_size), sizeof(uint64), 0);
				if (::send(socket, arg.data(), arg.size(), 0) == SOCKET_ERROR) {
					log<LogType::ERROR>(format{} << "Failed to send message: " << message.content);
				}
			}
			msg.clear();
        }
    };

	bool receive(SOCKET socket, char* data, uint64& size) {
		int r = recv(socket, reinterpret_cast<char*>(&size), sizeof(uint64), 0);
		if (r <= 0) return false;

		uint64 received = 0;
		while (received < size) {
			int r = recv(socket, data + received, size - received, 0);
			if (r <= 0) return false;
			received += r;
		}

		return true;
	}

	Message parse(const char* buffer, size len) {
		std::string str(buffer, len);

		size pos = str.find('\2');
		if (pos == std::string::npos) return { Str(str), {} };

		Message message;
		message.content = Str(str.substr(0, pos));

		std::string args_str = str.substr(pos + 1);
		size start = 0;

		while (true) {
			size end = args_str.find('\1', start);
			if (end == std::string::npos) {
				message.arguments.push_back(args_str.substr(start));
				break;
			}
			message.arguments.push_back(args_str.substr(start, end - start));
			start = end + 1;
		}

		return message;
	}
}