module;

#include <defs.hpp>

NO_WARNING
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#undef ERROR
DO_WARNING

export module game.network;

import std;

import misc.str;
import misc.list;
import misc.range;
import misc.number;
import misc.format;
import game.logger;

using namespace std::chrono_literals;

export namespace craftbuild {
    struct Message {
        Str content;
        std::vector<std::string> arguments;
    };

    struct SendQueue {
        std::vector<Message> msg;
        mutable std::mutex msg_mutex;

        void store(Message const& message);
        void send(SOCKET socket);
    };

    enum class ReceiveState {
        COMPLETE,
        WAITING,
        ERROR
    };

    struct ReceiveQueue {
        List<char> buffer;

        ReceiveState receive(SOCKET socket, List<char>& out_data);
        static Message parse(List<char> buffer);
    };
}