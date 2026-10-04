module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
ENABLE_WARNING

export module game.main;

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
import game.world;
import game.logger;
import game.thread;
import game.network;
import game.environment;
import game.world.cave;
import game.world.chunk;
import game.world.biome;
import game.block.redstone;
import game.block.block_data;
import game.player.player_data;
import game.block.normal_blocks;
import game.texture.atlas_texture;

using namespace godot;

export namespace craftbuild {
    class Main final : public Node3D {
        GDCLASS(Main, Node3D)

    private:
        List<Pos2D<i32>> ready_chunks_queue;
        mutable std::mutex ready_chunks_queue_mutex;

        Dict<Pos2D<i32>, std::pair<Ptr<Chunk>, ChunkRender>> chunks;
        mutable std::shared_mutex chunks_mutex;

        Set<Pos2D<i32>> requested_chunks;
        mutable std::mutex requested_chunks_mutex;

        Ref<ShaderMaterial> world_material;
        std::atomic<i32> world_seed = 0;
        Str world_name = "My World";
        Str player_name = "Player";
        std::tuple<String, i32> server_socket = std::tuple(String("127.0.0.1"), 8888);

        void* player_ptr = nullptr;
        mutable std::shared_mutex player_mutex;

        std::atomic<bool> running = true;
        std::atomic<fsize> player_x = 0.0;
        std::atomic<fsize> player_y = 0.0;
        std::atomic<fsize> player_z = 0.0;
        std::jthread gc_thread;
        std::jthread log_thread;
        std::jthread network_thread;
        std::jthread scheduler_thread;
        ThreadPool mesh_pool{ 4 };
        Set<Pos2D<i32>> pending_mesh_jobs;
        mutable std::mutex pending_jobs_mutex;

        std::atomic<bool> pausing = true;
        std::atomic<bool> chatting = false;
        std::condition_variable loop_cv;
        mutable std::mutex loop_mutex;

        bool full_screen = false;

        SendQueue send_queue;
        ReceiveQueue receive_queue;
		Ptr<World> server_ptr;

    public:
        void _ready() override;
        void _process(f64 delta) override;
        void _exit_tree() override;

        void init_singleplayer();
        void init_multiplayer();
        void setup_voxel_material();

        void start_gc_thread();
        void start_log_thread();
        void start_network_thread();
        void start_scheduler_thread();
        void submit_jobs();
        void create_chunk_collision(ChunkRender& chunk_render, PackedVector3Array const& collision_faces);
        void update_chunk_mesh(ChunkRender& chunk_render, Pos2D<i32>& pos, Ref<ArrayMesh> const& mesh, i32 submesh_idx);
        void unload_distant_chunks();

        Ptr<Chunk> get_chunk(i32 cx, i32 cy);
        Ptr<Chunk> get_or_create_chunk(i32 cx, i32 cy);
        ChunkRender& ref_mesh(i32 cx, i32 cy);
        u32 get_global_block_id(i32 wx, i32 wy, i32 wz);
        void set_chunk(Ptr<Chunk>& chunk, i32 cx, i32 cy);
        void set_global_block_id(u32 block_id, i32 wx, i32 wy, i32 wz);

        void save_userdata(char const* path = "user://game/userdata.cbdata");
        bool load_userdata(char const* path = "user://game/userdata.cbdata");

        void pause();
        void resume();
        void start_chat();

        void chat(const String msg);

        Vector3 get_player_position();
        void set_seed_and_world_name(i32 seed, const String name);
        void set_render_distance(i32 rd);
        void set_cpu_sleep_time(i32 stc);
        void set_server_socket(String ip, int32_t port);

        static void _bind_methods();

        friend class Player;
        friend class CommandInterpreter;
    };
}
