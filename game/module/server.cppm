module;

#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#undef ERROR

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>

#include <includes.hpp>
#include <string>
#include <sstream>
#include <shared_mutex>
#include <unordered_set>
#include <unordered_map>

export module game.server;

import misc.ptr;
import misc.str;
import misc.dict;
import misc.list;
import misc.range;
import misc.number;
import misc.format;
import misc.pos;
import game.core;
import game.block;
import game.logger;
import game.thread;
import game.network;
import game.world.cave;
import game.world.chunk;
import game.world.biome;
import game.player.player_data;
import game.block.normal_blocks;

using namespace godot;

export namespace craftbuild {
	class TCPServer {
        Dict<Pos3D<int32>, Ptr<Chunk>> chunks;
        mutable std::shared_mutex chunks_mutex;

        Ref<FastNoiseLite> noise;
        std::atomic<int32> world_seed = 0;
        Str world_name = "My World";

        Dict<Str, PlayerData> players;
        Dict<Str, uint8> online_players;
        decltype(online_players.begin()) current_player;
        none* command_ptr = nullptr;
        mutable std::shared_mutex player_mutex;
        mutable std::mutex current_player_mutex;

        std::atomic<bool> running = true;
        std::thread log_thread;
        std::thread redstone_thread;
        std::thread scheduler_thread;
        ThreadPool terrain_pool{ 4 };
        std::unordered_set<Pos3D<int>, Hasher<Pos3D<int>>> pending_terrain_jobs;
        mutable std::mutex pending_jobs_mutex;

        std::atomic<bool> pausing = true;
        std::atomic<bool> chatting = false;
        std::condition_variable loop_cv;
        mutable std::mutex loop_mutex;

    public:
        inline static int32 SIZE_X = render_distance * 16;
        inline static int32 SIZE_Z = render_distance * 16;

        TCPServer();
        ~TCPServer();
        none connect(const Str& player_name);
        none disconnect(const Str& player_name);
        none update(const Str& player_name, const Pos3D<real>& new_pos);

        none start_log_thread();
        none start_redstone_thread();
        none start_scheduler_thread();
        none submit_jobs(const Pos3D<real>& player);

        std::string serialize_players();
        std::string serialize_chunk(int cx, int cz);
        Ptr<Chunk> get_chunk(int cx, int cz);
        Ptr<Chunk>& get_or_create_chunk(int cx, int cz);
        uint32 get_global_block_id(int wx, int wy, int wz);
        none set_global_block_id(uint32 block_id, int wx, int wy, int wz);

        none set_seed_and_world_name(int32 seed, const Str& name);
        none set_render_distance(int32 rd);
        none set_sleep_time_cpu(int32 stc);

        Str chat(const Str& message);

        none save_world(const Str& path);
        bool load_world(const Str& path);

        friend class Main;
        friend class CommandInterpreter;
        friend none craftbuild_mod_main();
	};

    struct Client {
        ReceiveQueue receive_queue;
        SendQueue send_queue;
        Str name;
    };

    class Server : public Node {
        GDCLASS(Server, Node)

        WSADATA wsa_data;
        SOCKET server_socket;
        sockaddr_in server_addr{};
        TCPServer server;
		Dict<SOCKET, Client> clients;

    public:
        none _ready() override;
        none _process(double delta) override;
        none _exit_tree() override;

		static none _bind_methods() {}
    };
}