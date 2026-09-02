module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/stream_peer_tcp.hpp>
ENABLE_WARNING

export module game.network;

import std;

import misc.str;
import misc.list;
import misc.range;
import misc.number;
import misc.format;
import game.logger;

using namespace godot;
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
        void send(StreamPeerTCP& peer);
    };

    enum class ReceiveState {
        COMPLETE,
        WAITING,
        ERROR
    };

    struct ReceiveQueue {
        List<char> buffer;

        ReceiveState receive(StreamPeerTCP& peer, List<char>& out_data);
        static Message parse(List<char> buffer);
    };
}