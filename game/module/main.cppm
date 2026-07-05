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
import game.server_ptr;
import game.network;
import game.environment;
import game.world.cave;
import game.world.chunk;
import game.world.biome;
import game.player.player_data;
import game.block.normal_blocks;
import game.texture.atlas_texture;

using namespace godot;

export namespace craftbuild {
    class Main : public Node3D {
        GDCLASS(Main, Node3D)

    private:
        std::vector<Ptr<Chunk>> ready_chunks_queue;
        mutable std::mutex ready_chunks_queue_mutex;

        Dict<Pos3D<int32>, Ptr<Chunk>> chunks;
        mutable std::shared_mutex chunks_mutex;

        Set<Pos3D<int32>> requested_chunks;
        std::mutex requested_chunks_mutex;

        Ref<ShaderMaterial> world_material;
        std::atomic<int32> world_seed = 0;
        Str world_name = "My World";
        Str player_name = "Player";

        none* player_ptr = nullptr;
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
        none _ready() override;
        none _process(float64 delta) override;
        none _exit_tree() override;

        none init_singleplayer();
        none init_multiplayer();
        none setup_voxel_material();

        none start_gc_thread();
        none start_log_thread();
        none start_network_thread();
        none start_scheduler_thread();
        none submit_jobs();
        none create_chunk_collision(const Ptr<Chunk>& chunk, const PackedVector3Array& collision_faces);
        none update_chunk_mesh(const Ptr<Chunk>& chunk, const Ref<ArrayMesh>& mesh, PackedVector3Array& collision_faces);
        none unload_distant_chunks(int32 p_cx, int32 p_cz);

        Ptr<Chunk> get_chunk(int32 cx, int32 cz);
        Ptr<Chunk> get_or_create_chunk(int32 cx, int32 cz);
        uint32 get_global_block_id(int32 wx, int32 wy, int32 wz);
        none set_chunk(Ptr<Chunk> chunk, int32 cx, int32 cz);
        none set_global_block_id(uint32 block_id, int32 wx, int32 wy, int32 wz);

        none save_userdata(const char* path = "user://game/userdata.cbdata");
        bool load_userdata(const char* path = "user://game/userdata.cbdata");

        none pause();
        none resume();
        none start_chat();

        none chat(const String msg);

        none get_seed();
        none get_world_name();
        none set_seed_and_world_name(int32 seed, const String name);
        none set_render_distance(int32 rd);
        none set_sleep_time_cpu(int32 stc);

        static none _bind_methods();

        friend class Player;
        friend class CommandInterpreter;
        friend none craftbuild_mod_main();
    };
}
