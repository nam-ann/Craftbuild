module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/tcp_server.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/stream_peer_tcp.hpp>
ENABLE_WARNING

export module game.world;

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
import game.world.chunk;
import game.player.player_data;

using namespace godot;

export namespace craftbuild {
    class World final {
        Dict<Pos2D<i32>, Ptr<Chunk>> chunks;
        mutable std::shared_mutex chunks_mutex;

        Ref<FastNoiseLite> noise;
        std::atomic<i32> world_seed = 0;
        Str world_name = "My World";

        Dict<Str, PlayerData> players;
        Dict<Str, u8> online_players;
        decltype(online_players.begin()) current_player;
        void* command_ptr = nullptr;
        mutable std::shared_mutex player_mutex;
        mutable std::mutex current_player_mutex;

        std::atomic<bool> running = true;
        std::jthread redstone_thread;
        std::jthread scheduler_thread;
        ThreadPool terrain_pool{ 4 };
        Set<Pos2D<i32>> pending_terrain_jobs;
        mutable std::mutex pending_jobs_mutex;

        std::atomic<bool> pausing = true;
        std::atomic<bool> chatting = false;
        std::condition_variable loop_cv;
        mutable std::mutex loop_mutex;

    public:
        inline static i32 RANGE = render_distance * 16;

        void _get_refs(List<GCObject*>& refs);

        World();
        ~World();
        void connect(Str const& player_name);
        void disconnect(Str const& player_name);
        void update(Str const& player_name, Pos3D<fsize> const& new_pos);

        void start_redstone_thread();
        void start_scheduler_thread();
        void submit_jobs(Pos3D<fsize> const& player);

        std::string serialize_players();
        std::string serialize_chunk(i32 cx, i32 cy);
        Ptr<Chunk> get_chunk(i32 cx, i32 cy);
        Ptr<Chunk> get_or_load_chunk(i32 cx, i32 cy);
        Ptr<Chunk> get_or_create_chunk(i32 cx, i32 cy);
        u32 get_global_block_id(i32 wx, i32 wy, i32 wz);
        void set_global_block_id(u32 block_id, i32 wx, i32 wy, i32 wz);
        void unload_distant_chunks();

        void set_seed_and_world_name(i32 seed, Str const& name);
        void set_render_distance(i32 rd);
        void set_cpu_sleep_time(i32 stc);

        Str chat(Str const& message);

        void save_world(Str const& path);
        bool load_world(Str const& path);
        void save_region(Str const& path, i32 rx, i32 ry);
        bool load_region(Str const& path, i32 rx, i32 ry);

        friend class Main;
        friend class Server;
        friend class CommandInterpreter;
    };
}