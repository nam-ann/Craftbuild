module;

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#undef ERROR

#include <includes.hpp>

export module game.network;

import misc.str;
import misc.list;
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

    enum class ReceiveState {
        COMPLETE,
        WAITING,
        ERROR
    };
    struct ReceiveQueue {
        enum class State {
            READING_HEADER,
            READING_PAYLOAD
        };

        State current_state = State::READING_HEADER;
        uint64_t expected_size = 0;
        size_t current_offset = 0;
        List<char> buffer;

        ReceiveState receive(SOCKET socket, List<char>& out_data) {
            if (current_state == State::READING_HEADER) {
                if (len(buffer) < sizeof(uint64_t)) {
                    buffer.archive(sizeof(uint64_t));
                }

                int r = recv(socket, buffer.c_ptr() + current_offset, sizeof(uint64_t) - current_offset, 0);

                if (r > 0) {
                    current_offset += r;
                    if (current_offset == sizeof(uint64_t)) {
                        expected_size = *reinterpret_cast<uint64_t*>(buffer.c_ptr());
                        current_state = State::READING_PAYLOAD;
                        current_offset = 0;

                        buffer.resize(expected_size);
                    }
                }
                else if (r == 0) return ReceiveState::ERROR;
                else return ReceiveState::WAITING;
            }

            if (current_state == State::READING_PAYLOAD) {
                if (expected_size == 0) {
                    out_data.clear();
                    reset_state();
                    return ReceiveState::COMPLETE;
                }

                int r = recv(socket, buffer.c_ptr() + current_offset, expected_size - current_offset, 0);

                if (r > 0) {
                    current_offset += r;
                    if (current_offset == expected_size) {
                        out_data = std::move(buffer);
                        reset_state();
                        return ReceiveState::COMPLETE;
                    }
                }
                else if (r == 0) return ReceiveState::ERROR;
                else return ReceiveState::WAITING;
            }

            return ReceiveState::WAITING;
        }

        static Message parse(List<char> buffer) {
            std::string str(buffer, len(buffer));

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

    private:
        void reset_state() {
            current_state = State::READING_HEADER;
            current_offset = 0;
            expected_size = 0;
            buffer.clear();
        }
	};
}