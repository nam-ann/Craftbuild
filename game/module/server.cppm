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
import game.block.redstone;
import game.player.player_data;
import game.block.normal_blocks;

using namespace godot;

export namespace craftbuild {
	class GameEngine {
        Dict<Pos2D<int32>, Ptr<Chunk>> chunks;
        mutable std::shared_mutex chunks_mutex;

        Ref<FastNoiseLite> noise;
        std::atomic<int32> world_seed = 0;
        Str world_name = "My World";

        Dict<Str, PlayerData> players;
        Dict<Str, uint8> online_players;
        decltype(online_players.begin()) current_player;
        void* command_ptr = nullptr;
        mutable std::shared_mutex player_mutex;
        mutable std::mutex current_player_mutex;

        std::atomic<bool> running = true;
        std::thread redstone_thread;
        std::thread scheduler_thread;
        ThreadPool terrain_pool{ 4 };
        Set<Pos2D<int32>> pending_terrain_jobs;
        mutable std::mutex pending_jobs_mutex;

        std::atomic<bool> pausing = true;
        std::atomic<bool> chatting = false;
        std::condition_variable loop_cv;
        mutable std::mutex loop_mutex;

    public:
        inline static int32 RANGE = render_distance * 16;

        void _get_refs(std::vector<GCObject*>& refs);

        GameEngine();
        ~GameEngine();
        void connect(Str const& player_name);
        void disconnect(Str const& player_name);
        void update(Str const& player_name, Pos3D<real> const& new_pos);

        void start_redstone_thread();
        void start_scheduler_thread();
        void submit_jobs(Pos3D<real> const& player);

        std::string serialize_players();
        std::string serialize_chunk(int32 cx, int32 cy);
        Ptr<Chunk> get_chunk(int32 cx, int32 cy);
        Ptr<Chunk> get_or_load_chunk(int32 cx, int32 cy);
        Ptr<Chunk> get_or_create_chunk(int32 cx, int32 cy);
        uint32 get_global_block_id(int32 wx, int32 wy, int32 wz);
        void set_global_block_id(uint32 block_id, int32 wx, int32 wy, int32 wz);
        void unload_distant_chunks();

        void set_seed_and_world_name(int32 seed, Str const& name);
        void set_render_distance(int32 rd);
        void set_cpu_sleep_time(int32 stc);

        Str chat(Str const& message);

        void save_world(Str const& path);
        bool load_world(Str const& path);
        void save_region(Str const& path, int32 rx, int32 ry);
        bool load_region(Str const& path, int32 rx, int32 ry);

        friend class Main;
        friend class Server;
        friend class CommandInterpreter;
        friend void craftbuild_mod_main();
	};

    struct Client {
        ReceiveQueue receive_queue;
        SendQueue send_queue;
        Str name;
    };

    class Server : public Node {
        GDCLASS(Server, Node)

        Ref<TCPServer> tcp_server;
        GameEngine server;
        Dict<Ref<StreamPeerTCP>, Client> clients;

		std::atomic<bool> running = true;
        std::thread gc_thread;
        std::thread log_thread;

    public:
        void _ready() override;
        void _process(float64 delta) override;
        void _exit_tree() override;

        void start_gc_thread();
        void start_log_thread();

		static void _bind_methods() {}
    };
}