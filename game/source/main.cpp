module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/stream_peer_tcp.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/variant/node_path.hpp>
ENABLE_WARNING

module game.main;

import game.player;
import game.command;

using namespace std::chrono_literals;

namespace craftbuild {
    void Main::_ready() {
        start_gc_thread();
        start_log_thread();
        
        BlockRegistry::register_block<Air>          ("Air"           , "");
        BlockRegistry::register_block<Grass>        ("Grass Block"   , "grass_block.png");
        BlockRegistry::register_block<Dirt>         ("Dirt"          , "dirt.png");
        BlockRegistry::register_block<Stone>        ("Stone"         , "stone.png");
        BlockRegistry::register_block<Pebble>       ("Pebble"        , "pebble.png");
        BlockRegistry::register_block<OakLog>       ("Oak Log"       , "oak_log.png");
        BlockRegistry::register_block<OakPlanks>    ("Oak Planks"    , "oak_planks.png");
        BlockRegistry::register_block<OakLeaves>    ("Oak Leaves"    , "oak_leaves.png");
        BlockRegistry::register_block<DiamondBlock> ("Diamond Block" , "diamond_block.png");
        BlockRegistry::register_block<DiamondOre>   ("Diamond Ore"   , "diamond_ore.png");
        BlockRegistry::register_block<Bedrock>      ("Bedrock"       , "bedrock.png");
        BlockRegistry::register_block<RedstoneBlock>("Redstone Block", "redstone_block.png");
        BlockRegistry::register_block<RedstoneDust> ("Redstone Dust" , "redstone_dust.glb");

        Biome plains;
        plains.base_height = 5.0f;
        plains.base_noise = 0.1f;
        plains.detail_height = 4.0f;
        plains.detail_noise = 0.2f;
        plains.min_height = 40;

        Biome normal;
        normal.base_height = 60.0f;
        normal.base_noise = 0.05f;
        normal.detail_height = 8.0f;
        normal.detail_noise = 0.5f;
        normal.min_height = 40;

        Biome mountains;
        mountains.base_height = 140.0f;
        mountains.base_noise = 0.02f;
        mountains.detail_height = 35.0f;
        mountains.detail_noise = 0.15f;
        mountains.min_height = 40;

        Biome jagged_peaks;
        jagged_peaks.base_height = 180.0f;
        jagged_peaks.base_noise = 0.01f;
        jagged_peaks.detail_height = 60.0f;
        jagged_peaks.detail_noise = 0.4f;
        jagged_peaks.min_height = 60;

        Biome cherry_grove;
        cherry_grove.base_height = 50.0f;
        cherry_grove.base_noise = 0.08f;
        cherry_grove.detail_height = 15.0f;
        cherry_grove.detail_noise = 0.25f;
        cherry_grove.min_height = 45;

        BiomeRegistry::register_biome("Plains", plains);
        BiomeRegistry::register_biome("Normal", normal);
        BiomeRegistry::register_biome("Mountains", mountains);
        BiomeRegistry::register_biome("Jagged Peaks", jagged_peaks);
        BiomeRegistry::register_biome("Cherry Grove", cherry_grove);

        CaveRegistry::register_cave("Large Cavern", { CaveType::CHEESE, 0.5f, 0.02f });
        CaveRegistry::register_cave("Standard Tunnel", { CaveType::SPAGHETTI, 0.45f, 0.05f });
        CaveRegistry::register_cave("Deep Noodle", { CaveType::NOODLE, 0.35f, 0.08f });

        {
            std::unique_lock lock(player_mutex);
            player_ptr = get_node<Player>("Player");
        }
        if (not load_userdata()) log<LogType::WARNING>("Userdata file not found.");

        AtlasTexture::build_texture_array();
        setup_voxel_material();

        log<LogType::INFO>("Main initialized");
    }

    void Main::_process(float64 delta) {
        Player* player = static_cast<Player*>(player_ptr);
        if (not player) return;

		const auto player_pos = player->get_global_position();
        player_x.store(player_pos.x, std::memory_order_relaxed);
        player_y.store(player_pos.y, std::memory_order_relaxed);
        player_z.store(player_pos.z, std::memory_order_relaxed);

        static Pos3D<real> last_sent_pos;
        if ((player_pos - last_sent_pos) > Pos3D<real>(0.01f, 0.01f, 0.01f)) {
            if (not server_ptr) {
                last_sent_pos = player_pos;
                send_queue.store({ "Update player pos", { player_name.std_str(), std::to_string(player_pos.x), std::to_string(player_pos.y), std::to_string(player_pos.z)} });
            }
            else server_ptr.value().update(player_name, player_pos);
        }

        static auto last_sent_get_player_request = std::chrono::high_resolution_clock::now();
        if (auto elapsed = std::chrono::high_resolution_clock::now() - last_sent_get_player_request; elapsed >= 1s) {
            if (not server_ptr) {
                last_sent_get_player_request = std::chrono::high_resolution_clock::now();
                send_queue.store({ "Get players data", { player_name.std_str() } });
            }
            else if (Player* player = static_cast<Player*>(player_ptr)) {
                std::unique_lock lock(player_mutex);
                auto& player_data = server_ptr.value().players[player_name];

                player->hp = player_data.hp;
                std::memcpy(&player->hotbar, &player_data.hotbar, sizeof(uint32) * PlayerData::HOTBAR_SIZE);
            }
        }

        List<Pos2D<int32>> chunks_to_upload;
        {
            std::lock_guard lock(ready_chunks_queue_mutex);
            ready_chunks_queue.swap(chunks_to_upload);
        }

        List<Pos2D<int32>> deferred_chunks;
        {
            PackedVector3Array vertices;
            PackedVector3Array normals;
            PackedInt32Array indices;
            PackedVector2Array uvs;
            PackedVector2Array uvs_layer;
            PackedVector3Array collision_faces;

            constexpr auto max_updates = 8;
            int32 updates_this_frame = 0;

            for (auto& chunk_pos : chunks_to_upload) {
                if (updates_this_frame >= max_updates) {
                    deferred_chunks.append(chunk_pos);
                    continue;
                }

                auto& chunk_render = ref_mesh(chunk_pos.x, chunk_pos.y);

                Ptr<MeshData> data_ptr = nullptr;
                {
                    std::lock_guard lock(chunk_render.mesh_mutex);
                    if (chunk_render.pending_mesh_data) data_ptr.swap(chunk_render.pending_mesh_data);
                }

                if (not data_ptr) continue;
                auto& data = data_ptr.value();

                if (data.vertices) {
                    vertices.resize(len(data.vertices));
                    normals.resize(len(data.normals));
                    indices.resize(len(data.indices));
                    uvs.resize(len(data.uvs));
                    uvs_layer.resize(len(data.uvs_layer));
                    collision_faces.resize(len(data.collision_faces));

                    std::memcpy(vertices.ptrw(), data.vertices.data(), len(data.vertices) * sizeof(Pos3D<float32>));
                    std::memcpy(normals.ptrw(), data.normals.data(), len(data.normals) * sizeof(Pos3D<float32>));
                    std::memcpy(indices.ptrw(), data.indices.data(), len(data.indices) * sizeof(int32));
                    std::memcpy(uvs.ptrw(), data.uvs.data(), len(data.uvs) * sizeof(Pos2D<float32>));
                    std::memcpy(uvs_layer.ptrw(), data.uvs_layer.data(), len(data.uvs_layer) * sizeof(Pos2D<float32>));
                    std::memcpy(collision_faces.ptrw(), data.collision_faces.data(), len(data.collision_faces) * sizeof(Pos3D<float32>));

                    Array arrays;
                    arrays.resize(Mesh::ARRAY_MAX);

                    arrays[Mesh::ARRAY_VERTEX] = vertices;
                    arrays[Mesh::ARRAY_NORMAL] = normals;
                    arrays[Mesh::ARRAY_INDEX] = indices;
                    arrays[Mesh::ARRAY_TEX_UV] = uvs;
                    arrays[Mesh::ARRAY_TEX_UV2] = uvs_layer;

                    Ref<ArrayMesh> mesh;
                    mesh.instantiate();

                    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
                    update_chunk_mesh(chunk_render, chunk_pos, mesh);
                    create_chunk_collision(chunk_render, collision_faces);
                }

                if (not data.complex_instance) {
                    ++updates_this_frame;
                    continue;
                }

                if (not chunk_render.mesh_instance) update_chunk_mesh(chunk_render, chunk_pos, nullptr);
                if (not chunk_render.dynamic_body) {
                    chunk_render.dynamic_body = memnew(StaticBody3D);
                    chunk_render.mesh_instance->add_child(chunk_render.dynamic_body);
                }
                else while (chunk_render.dynamic_body->get_child_count() > 0) {
                    Node* child = chunk_render.dynamic_body->get_child(0);
                    chunk_render.dynamic_body->remove_child(child);
                    child->queue_free();
                }

                Dict<uint32, std::vector<ComplexBlockInstance>> grouped_instances;
                for (auto&& inst : data.complex_instance) {
                    Ref<BoxShape3D> box;
                    box.instantiate();
                    box->set_size(Vector3(1.0f, 0.1f, 1.0f));

                    CollisionShape3D* col_shape = memnew(CollisionShape3D);
                    col_shape->set_shape(box);
                    col_shape->set_position(Vector3(inst.local_pos.x + 0.5f, inst.local_pos.y + 0.5f, inst.local_pos.z + 0.5f));

                    chunk_render.dynamic_body->add_child(col_shape);
                    grouped_instances[inst.block_id].emplace_back(std::move(inst));
                }

                for (auto const& [id, instances] : grouped_instances) {
                    if (id >= BlockRegistry::registry.size()) continue;

                    Ref<MultiMesh> multimesh;
                    multimesh.instantiate();
                    multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
                    multimesh->set_mesh(BlockRegistry::registry[id].mesh);
                    multimesh->set_instance_count((int32)instances.size());

                    for (usize i : range<usize>(instances.size())) {
                        auto const& inst = instances[i];
                        Transform3D transform;
                        transform.origin = Vector3(inst.local_pos.x, inst.local_pos.y + 0.01f, inst.local_pos.z);
                        multimesh->set_instance_transform((int32)i, transform);
                    }

                    if (not chunk_render.multi_mesh_instance) {
                        chunk_render.multi_mesh_instance = memnew(MultiMeshInstance3D);
                        chunk_render.mesh_instance->add_child(chunk_render.multi_mesh_instance);
                    }

                    chunk_render.multi_mesh_instance->set_multimesh(multimesh);
                }

                ++updates_this_frame;
            }
        }

        {
            std::lock_guard lock(ready_chunks_queue_mutex);
            for (auto& chunk_pos : deferred_chunks) ready_chunks_queue.append(chunk_pos);
        }
    }

    void Main::_exit_tree() {
		running.store(false, std::memory_order_relaxed);

        server_ptr.clear();
        if (gc_thread.joinable()) gc_thread.join();
        if (log_thread.joinable()) log_thread.join();
        if (network_thread.joinable()) network_thread.join();
        if (scheduler_thread.joinable()) scheduler_thread.join();
        loop_cv.notify_all();

        save_userdata();
    }

    void Main::init_singleplayer() {
        log<LogType::INFO>("Starting local server...");
        server_ptr = new Obj<GameEngine>();
        server_ptr.value().connect(player_name);

        if (auto player = static_cast<Player*>(player_ptr)) {
            std::unique_lock lock(player_mutex);
            player->set_global_position(server_ptr.value().players[player_name].pos);
        }

        start_scheduler_thread();
    }

    void Main::init_multiplayer() {
        start_network_thread();
        start_scheduler_thread();
    }

    void Main::setup_voxel_material() {
        Ref<ShaderMaterial> mat;
        mat.instantiate();

        String shader_path = "res://assets/shaders/visual.gdshader";

        Ref<Shader> shader;
        shader.instantiate();

        if (FileAccess::file_exists(shader_path)) {
            Ref<FileAccess> file = FileAccess::open(shader_path, FileAccess::READ);
            if (file.is_valid()) {
                String shader_code = file->get_as_text();
                shader->set_code(shader_code);
            }
			else log<LogType::ERROR>("Failed to open shader file at: "f << shader_path.utf8());
        }
        else log<LogType::ERROR>("Shader file not found at: "f << shader_path.utf8());

        mat->set_shader(shader);
        mat->set_shader_parameter("u_texture_array", AtlasTexture::atlas_texture);

        world_material = mat;
    }

    void Main::start_gc_thread() {
        if (gc_thread.joinable()) return;

        auto worker = [this]() {
            ThreadRegistry::register_thread("GC");
            log<LogType::INFO>("GC thread started");

            while (running.load(std::memory_order_relaxed)) {
                GarbageCollector::collect();
                std::this_thread::sleep_for(5s);
            }

            GarbageCollector::collect();
        };

        gc_thread = std::thread(worker);
    }

    void Main::start_log_thread() {
        if (log_thread.joinable()) return;

        auto worker = [this]() {
            ThreadRegistry::register_thread("Log");
            log<LogType::INFO>("Log thread started");

            while (running.load(std::memory_order_relaxed)) {
                LogQueue::flush();
                std::this_thread::sleep_for(1s);
            }

            LogQueue::flush();
        };

        log_thread = std::thread(worker);
    }

    void Main::start_network_thread() {
        if (network_thread.joinable()) return;

        auto worker = [this]() {
            ThreadRegistry::register_thread("Network");
            log<LogType::INFO>("Network thread started");

            Ref<StreamPeerTCP> client_peer;
            client_peer.instantiate();

            auto err = client_peer->connect_to_host("127.0.0.1", 8888);
            if (err != godot::OK) {
                log<LogType::ERROR>("Connect failed");
                return;
            }

            log<LogType::INFO>("Connecting to server...");

            while (running.load(std::memory_order_relaxed)) {
                client_peer->poll();
                auto status = client_peer->get_status();

                if (status == StreamPeerTCP::STATUS_CONNECTED) break;
                if (status == StreamPeerTCP::STATUS_ERROR or status == StreamPeerTCP::STATUS_NONE) {
                    log<LogType::ERROR>("Failed to connect to server_ptr");
                    return;
                }

                std::this_thread::sleep_for(10ms);
            }

            send_queue.store({ "Connect", { player_name.std_str() } });

            {
                List<char> buffer;
                while (running.load(std::memory_order_relaxed)) {
                    const auto recv_state = receive_queue.receive(**client_peer, buffer);

                    if (recv_state == ReceiveState::WAITING) {
                        std::this_thread::sleep_for(100ms);
                        continue;
                    }
                    if (recv_state == ReceiveState::ERROR) {
                        log<LogType::ERROR>("Lost connect to server");
                        client_peer->disconnect_from_host();
                        return;
                    }

                    Message message = ReceiveQueue::parse(buffer);

                    if (message.content == "Connected") {
                        log<LogType::INFO>("Connected to server");
                        Pos3D<real> player_pos{ std::stof(message.arguments[0]), std::stof(message.arguments[1]), std::stof(message.arguments[2]) };
                        if (auto player = static_cast<Player*>(player_ptr)) {
                            std::unique_lock lock(player_mutex);
                            player->set_global_position(player_pos);
                        }
                        break;
                    }
                }
            }

            while (running.load(std::memory_order_relaxed)) {
                send_queue.send(**client_peer);
                List<char> buffer;

                const auto recv_state = receive_queue.receive(**client_peer, buffer);
                if (recv_state == ReceiveState::WAITING) {
                    std::this_thread::sleep_for(100ms);
                    continue;
                }
                if (recv_state == ReceiveState::ERROR) {
                    log<LogType::ERROR>("Lost connect to server");
                    break;
                }

                Message message = ReceiveQueue::parse(buffer);

                try {
                    if (message.content == "Chunk version") {
                        uint8 chunk_version = std::stoi(message.arguments[0]);
                        auto cx = std::stoi(message.arguments[1]), cy = std::stoi(message.arguments[2]);

                        if (auto chunk = get_chunk(cx, cy); not chunk or chunk.value().chunk_version != chunk_version) send_queue.store({ "Get chunk data", { std::to_string(cx), std::to_string(cy) } });
                    }
                    else if (message.content == "Chunk data") {
                        // Unzip
                        if (len(buffer) < sizeof(uint32)) {
                            log<LogType::ERROR>("Received chunk data is too small("f << len(buffer) << "). Packet might be corrupted");
                            continue;
                        }

                        std::string base64_payload = message.arguments[0];
                        String godot_base64_str = base64_payload.c_str();
                        PackedByteArray payload = Marshalls::get_singleton()->base64_to_raw(godot_base64_str);

                        if (payload.size() < sizeof(uint32)) {
                            log<LogType::ERROR>("Received chunk data is too small("f << payload.size() << ") to contain uncompressed size. Packet might be corrupted");
                            continue;
                        }

                        uint32 uncompressed_size = 0;
                        memcpy(&uncompressed_size, payload.ptr(), sizeof(uint32));

                        size_t compressed_size = payload.size() - sizeof(uint32);
                        PackedByteArray compressed_pba;
                        compressed_pba.resize(compressed_size);
                        memcpy(compressed_pba.ptrw(), payload.ptr() + sizeof(uint32), compressed_size);

                        PackedByteArray decompressed_pba = compressed_pba.decompress(uncompressed_size, FileAccess::COMPRESSION_ZSTD);

                        if (decompressed_pba.is_empty() and uncompressed_size > 0) {
                            log<LogType::ERROR>("Failed to decompress chunk data. Packet might be corrupted");
                            continue;
                        }

                        // Deserialize
                        std::string world_data(reinterpret_cast<char const*>(decompressed_pba.ptr()), decompressed_pba.size());
                        std::stringstream is(world_data, std::ios::binary | std::ios::in);

                        auto cx = std::stoi(message.arguments[1]), cy = std::stoi(message.arguments[2]);
                        auto& chunk = get_or_create_chunk(cx, cy).value();
                        std::unique_lock data_lock(chunk.data_mutex);
                        chunk.clear();

                        is.read(reinterpret_cast<char*>(&chunk.blocks[0][0][0]), uint64(Chunk::WIDTH * Chunk::HEIGHT * Chunk::WIDTH * sizeof(uint8)));
                        log<LogType::VERBOSE>(""f << chunk.blocks[0][0][0]);

                        is.read(reinterpret_cast<char*>(&chunk.block_ids_size), sizeof(uint8));
                        is.read(reinterpret_cast<char*>(&chunk.block_ids), sizeof(uint32) * 256);

                        uint8 id2block_size = 0;
                        is.read(reinterpret_cast<char*>(&id2block_size), sizeof(uint8));
                        chunk.id2block.reserve(id2block_size);
                        for (auto j : range<uint8>(id2block_size)) {
                            uint32 global_id = 0;
                            is.read(reinterpret_cast<char*>(&global_id), sizeof(uint32));
                            is.read(reinterpret_cast<char*>(&chunk.id2block[global_id]), sizeof(uint8));
                        }

                        uint64 meta_size = 0;
                        is.read(reinterpret_cast<char*>(&meta_size), sizeof(uint64));

                        for (auto j : range(meta_size)) {
                            Pos3D<uint8> pos;

                            is.read(reinterpret_cast<char*>(&pos.x), sizeof(uint8));
                            is.read(reinterpret_cast<char*>(&pos.y), sizeof(uint8));
                            is.read(reinterpret_cast<char*>(&pos.z), sizeof(uint8));

                            auto& meta_storages = chunk.meta_ids[pos];

                            uint64 meta_storages_size = 0;
                            is.read(reinterpret_cast<char*>(&meta_storages_size), sizeof(uint64));

                            for (auto k : range(meta_storages_size)) {
                                uint64 name_size = 0;
                                uint64 data_size = 0;
                                is.read(reinterpret_cast<char*>(&name_size), sizeof(uint64));
                                is.read(reinterpret_cast<char*>(&data_size), sizeof(uint64));

                                auto& meta_storage = meta_storages.emplace_back();

                                meta_storage.name.resize(name_size);
                                meta_storage.data.resize(data_size);

                                is.read(reinterpret_cast<char*>(meta_storage.name.data()), name_size);
                                is.read(reinterpret_cast<char*>(meta_storage.data.data()), data_size);
                            }
                        }

                        uint64 extended_block_size = 0;
                        is.read(reinterpret_cast<char*>(&extended_block_size), sizeof(uint64));

                        for (auto j : range(extended_block_size)) {
                            Pos3D<uint8> pos;

                            is.read(reinterpret_cast<char*>(&pos.x), sizeof(uint8));
                            is.read(reinterpret_cast<char*>(&pos.y), sizeof(uint8));
                            is.read(reinterpret_cast<char*>(&pos.z), sizeof(uint8));

                            is.read(reinterpret_cast<char*>(&chunk.extended_block_id[pos]), sizeof(uint32));
                        }

                        is.read(reinterpret_cast<char*>(&chunk.chunk_version), sizeof(uint8));

                        chunk.generated.store(true, std::memory_order_release);
                        chunk.dirty.store(true, std::memory_order_release);

                        Pos2D<int32> offsets[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
                        for (auto& o : offsets) {
                            auto n = get_chunk(chunk.chunk_pos.x + o.x, chunk.chunk_pos.y + o.y);
                            if (n and n.value().generated.load(std::memory_order_acquire)) n.value().dirty.store(true);
                        }

                        std::lock_guard lock(requested_chunks_mutex);
                        requested_chunks.erase({ cx, cy });
                    }
                    else if (message.content == "Chunk not ready") {
                        std::lock_guard lock(requested_chunks_mutex);
                        requested_chunks.erase({ std::stoi(message.arguments[0]), std::stoi(message.arguments[1]) });
                    }
                    else if (message.content == "Players data") {
                        std::stringstream is(message.arguments[0]);

                        uint64 player_count = 0;
                        is.read(reinterpret_cast<char*>(&player_count), sizeof(uint64));

                        for (auto i : range<uint64>(player_count)) {
                            uint64 name_len = 0;
                            Str name;
                            PlayerData player_data;

                            is.read(reinterpret_cast<char*>(&name_len), sizeof(uint64));
                            name.resize(name_len);
                            is.read(reinterpret_cast<char*>(name.data()), name_len);
                            is.read(reinterpret_cast<char*>(&player_data.hp), sizeof(uint8));
                            is.read(reinterpret_cast<char*>(&player_data.pos), sizeof(Pos3D<real>));
                            is.read(reinterpret_cast<char*>(&player_data.hotbar), sizeof(uint32) * PlayerData::HOTBAR_SIZE);

                            if (name == player_name) {
                                Player* player = static_cast<Player*>(player_ptr);
                                if (not player) continue;

                                std::unique_lock lock(player_mutex);
                                player->hp = player_data.hp;
                                memcpy(&player->hotbar, &player_data.hotbar, sizeof(uint32) * PlayerData::HOTBAR_SIZE);
                            }
                        }
                    }
                    else if (message.content == "Chat response") call_deferred("emit_signal", "chat_output", message.arguments[0].c_str());
                }
                catch (std::exception const& e) {
                    log<LogType::ERROR>("Error processing message: "f << message.content << " - " << e.what());
                }

                std::this_thread::sleep_for(10ms);
            }

            client_peer->disconnect_from_host();
        };

        network_thread = std::thread(worker);
    }

    void Main::start_scheduler_thread() {
        scheduler_thread = std::thread([this]() {
            ThreadRegistry::register_thread("Mesh");
            log<LogType::INFO>("Mesh thread started");

            auto last_unload_time = std::chrono::high_resolution_clock::now();
            while (running.load(std::memory_order_relaxed)) {
                submit_jobs();

                if (auto now = std::chrono::high_resolution_clock::now(); std::chrono::duration_cast<std::chrono::seconds>(now - last_unload_time).count() >= 5) {
                    unload_distant_chunks();
                    last_unload_time = now;
                }

                std::unique_lock<std::mutex> lock(loop_mutex);
                loop_cv.wait_for(lock, std::chrono::milliseconds(cpu_sleep_time));
            }
        });
    }

    void Main::submit_jobs() {
        const int32 px = int32(std::floor(player_x.load() / Chunk::WIDTH));
        const int32 pz = int32(std::floor(player_z.load() / Chunk::WIDTH));

        auto get_chunk_server_or_not = +[](Main& self, Pos2D<int32> chunk_pos) {
            Ptr<Chunk> chunk = self.server_ptr.value().get_chunk(chunk_pos.x, chunk_pos.y);
            self.set_chunk(chunk, chunk_pos.x, chunk_pos.y);
            return chunk;
        };

        if (not server_ptr) {
            get_chunk_server_or_not = +[](Main& self, Pos2D<int32> chunk_pos) {
                std::lock_guard lock(self.requested_chunks_mutex);
                if (not self.requested_chunks.contains({ chunk_pos.x, chunk_pos.y })) {
                    self.send_queue.store({ "Get chunk version", { std::to_string(chunk_pos.x), std::to_string(chunk_pos.y) } });
                    self.requested_chunks.insert({ chunk_pos.x, chunk_pos.y });
                }
                return self.get_chunk(chunk_pos.x, chunk_pos.y);
            };
        }

        auto process_cell = [&](int32 x, int32 z) {
            Pos2D<int32> chunk_pos{ px + x, pz + z };
            Ptr<Chunk> chunk_ptr = get_chunk_server_or_not(*this, chunk_pos);

            if (not chunk_ptr) return;
            Chunk& chunk = chunk_ptr.value();

            set_chunk(chunk_ptr, chunk_pos.x, chunk_pos.y);

            if (not chunk.dirty.load(std::memory_order_acquire)) return;

            {
                std::lock_guard lock(pending_jobs_mutex);
                if (pending_mesh_jobs.contains(chunk_pos)) return;
                pending_mesh_jobs.insert(chunk_pos);
            }

            mesh_pool.enqueue([this, chunk_ptr]() {
                auto& chunk = chunk_ptr.value();

                if (running.load(std::memory_order_relaxed)) {
                    Ptr<Chunk> neighbors[4] = {
                        get_chunk(chunk.chunk_pos.x + 1, chunk.chunk_pos.y),
                        get_chunk(chunk.chunk_pos.x - 1, chunk.chunk_pos.y),
                        get_chunk(chunk.chunk_pos.x, chunk.chunk_pos.y + 1),
                        get_chunk(chunk.chunk_pos.x, chunk.chunk_pos.y - 1)
                    };

                    chunk.generate_mesh(ref_mesh(chunk.chunk_pos.x, chunk.chunk_pos.y), neighbors);

                    std::lock_guard lock(ready_chunks_queue_mutex);
                    ready_chunks_queue.append(chunk.chunk_pos);
                }

                std::lock_guard lock(pending_jobs_mutex);
                pending_mesh_jobs.erase(chunk.chunk_pos);
                std::this_thread::sleep_for(10ms);
            });
        };

        process_cell(0, 0);

        for (auto r : range<int32>(1, render_distance + 1)) {
            for (auto x : range<int32>(-r, r + 1)) {
                process_cell(x, -r);
                process_cell(x, r);
            }
            for (auto z : range<int32>(-r + 1, r)) {
                process_cell(-r, z);
                process_cell(r, z);
            }
        }
    }

    void Main::create_chunk_collision(ChunkRender& chunk_render, PackedVector3Array const& collision_faces) {
        if (not chunk_render.mesh_instance) return;

        Ref<ArrayMesh> mesh = chunk_render.mesh_instance->get_mesh();
        if (mesh.is_null() or mesh->get_surface_count() == 0) return;
        if (collision_faces.size() == 0) return;

        if (collision_faces.size() % 3 != 0) {
            log<LogType::ERROR>("Invalid faces size for collision: "f << collision_faces.size());
            return;
        }

        if (not chunk_render.collision_body) {
            chunk_render.collision_body = memnew(StaticBody3D);
            chunk_render.mesh_instance->add_child(chunk_render.collision_body);
        }

        if (not chunk_render.collision_shape) {
            chunk_render.collision_shape = memnew(CollisionShape3D);
            chunk_render.collision_body->add_child(chunk_render.collision_shape);
        }

        Ref<ConcavePolygonShape3D> concave;
        concave.instantiate();
        concave->set_faces(collision_faces);

        chunk_render.collision_shape->set_shape(concave);
    }

    void Main::update_chunk_mesh(ChunkRender& chunk_render, Pos2D<int32>& pos, Ref<ArrayMesh> const& mesh) {
        if (not chunk_render.mesh_instance) {
            MeshInstance3D* mi = memnew(MeshInstance3D);
            mi->set_position(Vector3(real(pos.x * Chunk::WIDTH), 0, real(pos.y * Chunk::WIDTH)));
            mi->set_material_override(world_material);
            add_child(mi);
            chunk_render.mesh_instance = mi;
        }

        chunk_render.mesh_instance->set_mesh(mesh);
    }

    void Main::unload_distant_chunks() {
        const int32 p_cx = int32(player_x.load() / Chunk::WIDTH);
        const int32 p_cy = int32(player_z.load() / Chunk::WIDTH);
        const int32 unload_dist = render_distance + 4;

        int32 removed = 0;
        {
            std::unique_lock lock(chunks_mutex);
            for (auto it = chunks.begin(); it != chunks.end();) {
                auto& [chunk_pos, chunk_pair] = *it;

                int32 dx = std::abs(chunk_pos.x - p_cx);
                int32 dz = std::abs(chunk_pos.y - p_cy);

                if (dx > unload_dist or dz > unload_dist) {
                    chunk_pair.second.clear();
                    it = chunks.erase(it);
                    ++removed;
                }
                else ++it;
            }
        }

        if (removed) log<LogType::VERBOSE>("Queued unload for "f << removed << " chunks");
    }

    Ptr<Chunk> Main::get_chunk(int32 cx, int32 cy) {
        Pos2D<int32> cpos(cx, cy);

        std::shared_lock lock(chunks_mutex);
        auto it = chunks.find(cpos);

        if (it == chunks.end()) return nullptr;
        return it->second.first;
    }

    Ptr<Chunk> Main::get_or_create_chunk(int32 cx, int32 cy) {
        Pos2D<int32> chunk_pos{ cx, cy };
        {
            std::shared_lock lock(chunks_mutex);
            auto it = chunks.find(chunk_pos);
            if (it != chunks.end()) return it->second.first;
        }

        std::unique_lock lock(chunks_mutex);

        Ptr<Chunk> chunk = new Obj<Chunk>();
        chunk.value().chunk_pos = chunk_pos;
        chunks[chunk_pos].first.swap(chunk);

        return chunks[chunk_pos].first;
    }

    ChunkRender& Main::ref_mesh(int32 cx, int32 cy) {
        Pos2D<int32> cpos(cx, cy);

        std::unique_lock lock(chunks_mutex);
        return chunks[cpos].second;
    }
    
    uint32 Main::get_global_block_id(int32 wx, int32 wy, int32 wz) {
        if (wy < 0 or wy >= Chunk::HEIGHT) return BlockRegistry::get_id("Air");

        int32 cx = int32(std::floor(float32(wx) / Chunk::WIDTH));
        int32 cy = int32(std::floor(float32(wz) / Chunk::WIDTH));
        Pos2D<int32> cpos(cx, cy);

        Ptr<Chunk> chunk = get_chunk(cx, cy);
        if (not chunk) return BlockRegistry::get_id("Air");

        int32 lx = (wx % Chunk::WIDTH + Chunk::WIDTH) % Chunk::WIDTH;
        int32 lz = (wz % Chunk::WIDTH + Chunk::WIDTH) % Chunk::WIDTH;

        return chunk.value().get_block({ uint8(lx), uint8(wy), uint8(lz) });
    }

    void Main::set_chunk(Ptr<Chunk>& chunk, int32 cx, int32 cy) {
        Pos2D<int32> cpos(cx, cy);

        std::unique_lock lock(chunks_mutex);
		chunks[cpos].first = chunk;
    }

    void Main::set_global_block_id(uint32 block_id, int32 wx, int32 wy, int32 wz) {
        if (wy < 0 or wy >= Chunk::HEIGHT) return;
        if (not server_ptr) send_queue.store({ "Set block", { std::to_string(block_id), std::to_string(wx), std::to_string(wy), std::to_string(wz) } });
        
        int32 cx = int32(std::floor(float32(wx) / Chunk::WIDTH));
        int32 cy = int32(std::floor(float32(wz) / Chunk::WIDTH));
        Pos2D<int32> cpos(cx, cy);

        Ptr<Chunk> chunk = get_chunk(cx, cy);
        if (not chunk) return;

        int32 lx = (wx % Chunk::WIDTH + Chunk::WIDTH) % Chunk::WIDTH;
        int32 lz = (wz % Chunk::WIDTH + Chunk::WIDTH) % Chunk::WIDTH;

        chunk.value().set_block({ uint8(lx), uint8(wy), uint8(lz) }, block_id);
    }

    void Main::save_userdata(char const* path) {
        String real_path = ProjectSettings::get_singleton()->globalize_path(path);
        std::string std_path = real_path.utf8().get_data();

        // Tạo thư mục
        std::filesystem::create_directories(std::filesystem::path(std_path).parent_path());

        std::ofstream ofs(std_path, std::ios::binary);
        if (not ofs.is_open()) {
            log<LogType::ERROR>("Cannot open save file: "f << std_path);
            return;
        }

        std::shared_lock lock(player_mutex);
        Player* player = static_cast<Player*>(player_ptr);
        if (not player) return;

        ofs.write(reinterpret_cast<char const*>(&full_screen), sizeof(bool));
        ofs.write(reinterpret_cast<char const*>(&player->sensitivity), sizeof(float32));
        ofs.write(reinterpret_cast<char const*>(&player->mouse_pitch), sizeof(float32));
        ofs.write(reinterpret_cast<char const*>(&render_distance), sizeof(int32));
    }

    bool Main::load_userdata(char const* path) {
        String real_path = ProjectSettings::get_singleton()->globalize_path(path);
        std::string std_path = real_path.utf8().get_data();

        std::ifstream ifs(std_path, std::ios::binary);
        if (not ifs.is_open()) return false;

        {
            std::shared_lock lock(player_mutex);
            Player* player = static_cast<Player*>(player_ptr);
            if (not player) return false;

            ifs.read(reinterpret_cast<char*>(&full_screen), sizeof(bool));
            ifs.read(reinterpret_cast<char*>(&player->sensitivity), sizeof(float32));
            ifs.read(reinterpret_cast<char*>(&player->mouse_pitch), sizeof(float32));
            ifs.read(reinterpret_cast<char*>(&render_distance), sizeof(int32));
        }

		log<LogType::INFO>("Userdata loaded");
        return true;
    }

    void Main::pause() {
        Input* input = Input::get_singleton();
        input->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
        pausing.store(true, std::memory_order_relaxed);
    }
    void Main::resume() {
        Input* input = Input::get_singleton();
        input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
        pausing.store(false, std::memory_order_relaxed);
    }

    void Main::start_chat() {
        Input* input = Input::get_singleton();
        input->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
        chatting.store(true, std::memory_order_relaxed);
    }

    void Main::chat(const String msg) {
        if (not server_ptr) send_queue.store({ "Chat", { (std::string)msg.utf8() } });
        else {
            Str output = server_ptr.value().chat((std::string)msg.utf8());
			emit_signal("chat_output", output.std_str().c_str());
        }
        Input* input = Input::get_singleton();
        input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
        chatting.store(false, std::memory_order_relaxed);
    }

    void Main::set_seed_and_world_name(int32 seed, const String name) {
        if (not server_ptr) send_queue.store({ "Set seed and world name", { std::to_string(seed), (std::string)name.utf8() } });
        else server_ptr.value().set_seed_and_world_name(seed, (std::string)name.utf8());
    }

    void Main::set_render_distance(int32 rd) {
        if (not server_ptr) send_queue.store({ "Set render distance", { std::to_string(rd) } });
        else server_ptr.value().set_render_distance(rd);
    }

    void Main::set_cpu_sleep_time(int32 stc) {
        if (not server_ptr) send_queue.store({ "Set sleep time CPU", { std::to_string(stc) } });
        else server_ptr.value().set_cpu_sleep_time(stc);
    }
    
    void Main::_bind_methods() {
        ADD_SIGNAL(MethodInfo("chat_output", PropertyInfo(Variant::STRING, "line")));
        ClassDB::bind_method(D_METHOD("init"), &Main::_ready);
        ClassDB::bind_method(D_METHOD("singleplayer"), &Main::init_singleplayer);
        ClassDB::bind_method(D_METHOD("multiplayer"), &Main::init_multiplayer);
        ClassDB::bind_method(D_METHOD("process"), &Main::_process);
        ClassDB::bind_method(D_METHOD("pause_game"), &Main::pause);
        ClassDB::bind_method(D_METHOD("resume_game"), &Main::resume);
        ClassDB::bind_method(D_METHOD("start_chat"), &Main::start_chat);
        ClassDB::bind_method(D_METHOD("chat", "msg"), &Main::chat);
        ClassDB::bind_method(D_METHOD("set_seed_and_world_name", "seed", "name"), &Main::set_seed_and_world_name);
        ClassDB::bind_method(D_METHOD("set_render_distance", "rd"), &Main::set_render_distance);
        ClassDB::bind_method(D_METHOD("set_cpu_sleep_time", "stc"), &Main::set_cpu_sleep_time);
    }
}
