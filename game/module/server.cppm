module;

#pragma warning(push, 0)
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#undef ERROR

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#pragma warning(pop)

#include <includes.hpp>
#include <string>
#include <sstream>
#include <shared_mutex>
#include <unordered_set>
#include <unordered_map>

export module game.server_ptr;

import misc.gc;
import misc.ptr;
import misc.str;
import misc.set;
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
        std::thread redstone_thread;
        std::thread scheduler_thread;
        ThreadPool terrain_pool{ 4 };
        Set<Pos3D<int32>> pending_terrain_jobs;
        mutable std::mutex pending_jobs_mutex;

        List<Pos3D<int32>> chunks_to_remove;
        std::mutex chunks_to_remove_mutex;
        std::atomic<bool> should_remove_chunks = false;
        std::atomic<bool> pausing = true;
        std::atomic<bool> chatting = false;
        std::condition_variable loop_cv;
        mutable std::mutex loop_mutex;

    public:
        inline static int32 SIZE_X = render_distance * 16;
        inline static int32 SIZE_Z = render_distance * 16;

        none _get_refs(std::vector<GCObject*>& refs);

        TCPServer();
        ~TCPServer();
        none connect(const Str& player_name);
        none disconnect(const Str& player_name);
        none update(const Str& player_name, const Pos3D<real>& new_pos);

        none start_redstone_thread();
        none start_scheduler_thread();
        none submit_jobs(const Pos3D<real>& player);

        std::string serialize_players();
        std::string serialize_chunk(int32 cx, int32 cz);
        Ptr<Chunk> get_chunk(int32 cx, int32 cz);
        Ptr<Chunk> get_or_load_chunk(int32 cx, int32 cz);
        Ptr<Chunk> get_or_create_chunk(int32 cx, int32 cz);
        uint32 get_global_block_id(int32 wx, int32 wy, int32 wz);
        none set_global_block_id(uint32 block_id, int32 wx, int32 wy, int32 wz);

        none set_seed_and_world_name(int32 seed, const Str& name);
        none set_render_distance(int32 rd);
        none set_sleep_time_cpu(int32 stc);

        Str chat(const Str& message);

        none save_world(const Str& path);
        bool load_world(const Str& path);
        none save_region(const Str& path, int32 rx, int32 rz);
        bool load_region(const Str& path, int32 rx, int32 rz);

        friend class Main;
        friend class Server;
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
        TCPServer server_ptr;
		Dict<SOCKET, Client> clients;

		std::atomic<bool> running = true;
        std::thread gc_thread;
        std::thread log_thread;

    public:
        none _ready() override;
        none _process(float64 delta) override;
        none _exit_tree() override;

        none start_gc_thread();
        none start_log_thread();

		static none _bind_methods() {}
    };
}