module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/tcp_server.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/stream_peer_tcp.hpp>
ENABLE_WARNING

export module game.server;

import std;

import misc.str;
import misc.dict;
import misc.list;
import misc.range;
import misc.number;
import misc.format;
import misc.pos;
import game.core;
import game.block;
import game.world;
import game.logger;
import game.network;
import game.world.cave;
import game.world.chunk;
import game.world.biome;
import game.block.redstone;
import game.block.block_data;
import game.block.normal_blocks;

using namespace godot;

export namespace craftbuild {
    struct Client {
        ReceiveQueue receive_queue;
        SendQueue send_queue;
        Str name;
        Str ip_addr;
    };

    class Server : public Node {
        GDCLASS(Server, Node)

        Ref<TCPServer> tcp_server;
        World server;
        Dict<Ref<StreamPeerTCP>, Client> clients;

		std::atomic<bool> running = true;
        std::jthread gc_thread;
        std::jthread log_thread;

    public:
        void _ready() override;
        void _process(float64 delta) override;
        void _exit_tree() override;

        void start_gc_thread();
        void start_log_thread();

		static void _bind_methods() {}
    };
}