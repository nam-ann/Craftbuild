module;

#pragma warning(push, 0)
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#pragma warning(pop)
 
#include <includes.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <deque>
#include <memory>
#include <unordered_set>

export module game.main;

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
import game.server;
import game.network;
import game.environment;
import game.world.cave;
import game.world.chunk;
import game.world.biome;
import game.block.redstone;
import game.player.player_data;
import game.block.normal_blocks;
import game.texture.atlas_texture;

using namespace godot;

export namespace craftbuild {
    class Main : public Node3D {
        GDCLASS(Main, Node3D)

    private:
        List<Pos3D<int32>> ready_chunks_queue;
        mutable std::mutex ready_chunks_queue_mutex;

        Dict<Pos3D<int32>, std::pair<Ptr<Chunk>, ChunkRender>> chunks;
        mutable std::shared_mutex chunks_mutex;

        Set<Pos3D<int32>> requested_chunks;
        std::mutex requested_chunks_mutex;

        Ref<ShaderMaterial> world_material;
        std::atomic<int32> world_seed = 0;
        Str world_name = "My World";
        Str player_name = "Player";

        void* player_ptr = nullptr;
        mutable std::shared_mutex player_mutex;

        std::atomic<bool> running = true;
        std::atomic<real> player_x = 0.0;
        std::atomic<real> player_y = 0.0;
        std::atomic<real> player_z = 0.0;
        std::thread gc_thread;
        std::thread log_thread;
        std::thread network_thread;
        std::thread scheduler_thread;
        ThreadPool mesh_pool{ 4 };
        Set<Pos3D<int32>> pending_mesh_jobs;
        std::mutex pending_jobs_mutex;

        std::atomic<bool> pausing = true;
        std::atomic<bool> chatting = false;
        std::mutex loop_mutex;
        std::condition_variable loop_cv;

        bool full_screen = false;

        SendQueue send_queue;
        ReceiveQueue receive_queue;
		Ptr<TCPServer> server_ptr;

    public:
        void _ready() override;
        void _process(float64 delta) override;
        void _exit_tree() override;

        void init_singleplayer();
        void init_multiplayer();
        void setup_voxel_material();

        void start_gc_thread();
        void start_log_thread();
        void start_network_thread();
        void start_scheduler_thread();
        void submit_jobs();
        void create_chunk_collision(ChunkRender& chunk_render, Pos3D<int32>& pos, PackedVector3Array const& collision_faces);
        void update_chunk_mesh(ChunkRender& chunk_render, Pos3D<int32>& pos, Ref<ArrayMesh> const& mesh);
        void unload_distant_chunks();

        Ptr<Chunk> get_chunk(int32 cx, int32 cz);
        Ptr<Chunk> get_or_create_chunk(int32 cx, int32 cz);
        ChunkRender& ref_mesh(int32 cx, int32 cz);
        uint32 get_global_block_id(int32 wx, int32 wy, int32 wz);
        void set_chunk(Ptr<Chunk>& chunk, int32 cx, int32 cz);
        void set_global_block_id(uint32 block_id, int32 wx, int32 wy, int32 wz);

        void save_userdata(char const* path = "user://game/userdata.cbdata");
        bool load_userdata(char const* path = "user://game/userdata.cbdata");

        void pause();
        void resume();
        void start_chat();

        void chat(const String msg);

        void get_seed();
        void get_world_name();
        void set_seed_and_world_name(int32 seed, const String name);
        void set_render_distance(int32 rd);
        void set_cpu_sleep_time(int32 stc);

        static void _bind_methods();

        friend class Player;
        friend class CommandInterpreter;
        friend void craftbuild_mod_main();
    };
}
