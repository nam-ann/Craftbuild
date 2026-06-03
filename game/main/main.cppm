module;

#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
 
#include <includes.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <deque>
#include <memory>
#include <unordered_set>

export module game.main;

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
import game.server;
import game.network;
import game.environment;
import game.world.cave;
import game.world.chunk;
import game.world.biome;
import game.block.normal_blocks;
import game.texture.atlas_texture;

using namespace godot;

export namespace craftbuild {
    class Main : public Node3D {
        GDCLASS(Main, Node3D)

    private:
        Dict<Pos3D<int32>, Ptr<Chunk>> chunks;
        mutable std::shared_mutex chunks_mutex;

        Ref<ShaderMaterial> world_material;
        std::atomic<int32> world_seed = 0;
        Str world_name = "My World";
        Str player_name = "Player";

        none* player_ptr = nullptr;

        std::atomic<bool> running = true;
        std::atomic<real> player_x = 0.0;
        std::atomic<real> player_y = 0.0;
        std::atomic<real> player_z = 0.0;
        std::thread log_thread;
        std::thread network_thread;
        std::thread scheduler_thread;
        ThreadPool mesh_pool{ 4 };
        std::unordered_set<Pos3D<int>, Hasher<Pos3D<int>>> pending_mesh_jobs;
        std::mutex pending_jobs_mutex;

        List<Pos3D<int>> chunks_to_remove;
        std::mutex chunks_to_remove_mutex;
        std::atomic<bool> should_remove_chunks = false;
        std::atomic<bool> pausing = true;
        std::atomic<bool> chatting = false;
        std::mutex loop_mutex;
        std::condition_variable loop_cv;

        bool full_screen = false;
        bool multiplayer = false;

        SendQueue send_queue;
        ReceiveQueue receive_queue;
		Ptr<TCPServer> server;

    public:
        none _ready() override;
        none _process(float64 delta) override;
        none _notification(int p_what);
        
        none setup_voxel_material();

        none start_log_thread();
        none start_network_thread();
        none start_scheduler_thread();
        none submit_jobs();
        none create_chunk_collision(Ptr<Chunk> chunk, const PackedVector3Array& collision_faces);
        none update_chunk_mesh(Ptr<Chunk> chunk, Ref<ArrayMesh> mesh, PackedVector3Array& collision_faces);
        none unload_distant_chunks(int p_cx, int p_cz);

        Ptr<Chunk> get_chunk(int cx, int cz);
        Ptr<Chunk> get_or_create_chunk(int cx, int cz);
        uint32 get_global_block_id(int wx, int wy, int wz);
        none set_chunk(Ptr<Chunk> chunk, int cx, int cz);
        none set_global_block_id(uint32 block_id, int wx, int wy, int wz);

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
