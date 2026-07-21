module;

#pragma warning(push, 0)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#undef ERROR

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
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/variant/node_path.hpp>
#pragma warning(pop)

#include <includes.hpp>
#include <cmath>
#include <thread>
#include <chrono>
#include <random>
#include <string>
#include <iomanip>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <shared_mutex>

module game.main;

using byte = char;
import game.player;
import game.command;

using namespace std::chrono_literals;

namespace craftbuild {
    void Main::_ready() {
        start_gc_thread();
        start_log_thread();
        
        TagRegistry::register_tag("face");
        TagRegistry::register_tag("transparent");
        TagRegistry::register_tag("collision_size");
        TagRegistry::register_tag("collision_offset");

        TagRegistry::set_value(TagRegistry::get_id("transparent"), 1, true);
        TagRegistry::set_value(TagRegistry::get_id("has_collision"), 1, true);
        TagRegistry::set_value(TagRegistry::get_id("collision_size"), 1, pack_vec3_mm(Pos3D<real>(0.2f, 0.6f, 0.2f)));
        TagRegistry::set_value(TagRegistry::get_id("collision_offset"), 1, pack_vec3_mm(Pos3D<real>(0.5f, 0.3f, 0.5f)));
        
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
                memcpy(&player->hotbar, &player_data.hotbar, sizeof(uint32) * PlayerData::HOTBAR_SIZE);
            }
        }

        constexpr int32 max_updates = 8;
        int32 updates_this_frame = 0;

        std::vector<Ptr<Chunk>> chunks_to_upload;
        {
            std::lock_guard lock(ready_chunks_queue_mutex);
            ready_chunks_queue.swap(chunks_to_upload);
        }

        std::vector<Ptr<Chunk>> deferred_chunks;
        for (auto& chunk_ptr : chunks_to_upload) {
            if (updates_this_frame >= max_updates) {
                deferred_chunks.push_back(chunk_ptr);
                continue;
            }

			if (not chunk_ptr) continue;
			auto& chunk = chunk_ptr.value();
               
            Ptr<MeshData> data_ptr = nullptr;
            {
                std::lock_guard lock(chunk.mesh_mutex);
                if (chunk.pending_mesh_data) data_ptr.swap(chunk.pending_mesh_data);
            }

            if (not data_ptr) continue;
            auto& data = data_ptr.value();

            Ref<ArrayMesh> mesh;
            mesh.instantiate();

            if (len(data.vertices) != 0) {
                Array arrays;
                arrays.resize(Mesh::ARRAY_MAX);

                PackedVector3Array vertices;
                vertices.resize(len(data.vertices));
                memcpy(vertices.ptrw(), data.vertices.c_ptr(), len(data.vertices) * sizeof(Pos3D<float32>));

                PackedVector3Array normals;
                normals.resize(len(data.normals));
                memcpy(normals.ptrw(), data.normals.c_ptr(), len(data.normals) * sizeof(Pos3D<float32>));

                PackedInt32Array indices;
                indices.resize(len(data.indices));
                memcpy(indices.ptrw(), data.indices.c_ptr(), len(data.indices) * sizeof(int32));

                PackedVector2Array uvs;
                uvs.resize(len(data.uvs));
                memcpy(uvs.ptrw(), data.uvs.c_ptr(), len(data.uvs) * sizeof(Vector2));

                PackedVector2Array uvs_layer;
                uvs_layer.resize(len(data.uvs_layer));
                memcpy(uvs_layer.ptrw(), data.uvs_layer.c_ptr(), len(data.uvs_layer) * sizeof(Vector2));

                PackedVector3Array collision_faces;
                collision_faces.resize(len(data.collision_faces));
                memcpy(collision_faces.ptrw(), data.collision_faces.c_ptr(), len(data.collision_faces) * sizeof(Pos3D<float32>));

                arrays[Mesh::ARRAY_VERTEX] = vertices;
                arrays[Mesh::ARRAY_NORMAL] = normals;
                arrays[Mesh::ARRAY_INDEX] = indices;
                arrays[Mesh::ARRAY_TEX_UV] = uvs;
                arrays[Mesh::ARRAY_TEX_UV2] = uvs_layer;

                mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
                update_chunk_mesh(chunk_ptr, mesh);
                create_chunk_collision(chunk_ptr, collision_faces);
            }

            if (not data.dyn_instances) {
                ++updates_this_frame;
                continue;
            }

            if (not chunk.mesh_instance) update_chunk_mesh(chunk_ptr, nullptr);
            if (not chunk.dynamic_body) {
                chunk.dynamic_body = memnew(StaticBody3D);
                chunk.mesh_instance->add_child(chunk.dynamic_body);
            }
            else while (chunk.dynamic_body->get_child_count() > 0) {
                Node* child = chunk.dynamic_body->get_child(0);
                chunk.dynamic_body->remove_child(child);
                child->queue_free();
            }

            Dict<uint32, std::vector<DynBlockInstance>> grouped_instances;
            for (auto&& inst : data.dyn_instances) {
                CollisionShape3D* col_shape = memnew(CollisionShape3D);
                Ref<BoxShape3D> box;
                box.instantiate();
                box->set_size(Vector3(1.0f, 0.1f, 1.0f));

                col_shape->set_shape(box);
                col_shape->set_position(Vector3(inst.local_pos.x + 0.5f, inst.local_pos.y + 0.5f, inst.local_pos.z + 0.5f));

                chunk.dynamic_body->add_child(col_shape);
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

                if (not chunk.multi_mesh_instance) {
                    chunk.multi_mesh_instance = memnew(MultiMeshInstance3D);
                    chunk.mesh_instance->add_child(chunk.multi_mesh_instance);
                }

                chunk.multi_mesh_instance->set_multimesh(multimesh);
            }

            ++updates_this_frame;
        }

        {
            std::lock_guard lock(ready_chunks_queue_mutex);
            for (auto& chunk_ptr : deferred_chunks) ready_chunks_queue.emplace_back(std::move(chunk_ptr));
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
        server_ptr = new Obj<TCPServer>();
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

        String shader_path = "res://assets/shaders/visual.cbshader";

        Ref<Shader> shader;
        shader.instantiate();

        if (FileAccess::file_exists(shader_path)) {
            Ref<FileAccess> file = FileAccess::open(shader_path, FileAccess::READ);
            if (file.is_valid()) {
                String shader_code = file->get_as_text();
                shader->set_code(shader_code);
            }
			else log<LogType::ERROR>(format{} << "Failed to open shader file at: " << shader_path.utf8());
        }
        else log<LogType::ERROR>(format{} << "Shader file not found at: " << shader_path.utf8());

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
                std::this_thread::sleep_for(std::chrono::seconds(5));
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
                std::this_thread::sleep_for(std::chrono::seconds(1));
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

            WSADATA wsa;
            WSAStartup(MAKEWORD(2, 2), &wsa);

            SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

            sockaddr_in server_addr{};
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(8888);
            inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

            if (::connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
                log<LogType::ERROR>("Connect failed");
                closesocket(client_socket);
                WSACleanup();
                return;
            }

            u_long mode = 1;
            if (ioctlsocket(client_socket, FIONBIO, &mode) != 0) {
                log<LogType::ERROR>("Failed to set socket to non-blocking mode");
                closesocket(client_socket);
                WSACleanup();
                return;
            }

            log<LogType::INFO>("Connecting to server_ptr...");
            send_queue.store({ "Connect", { player_name.std_str() } });

            {
                List<char> buffer;
                while (running.load(std::memory_order_relaxed)) {
                    const auto recv_state = receive_queue.receive(client_socket, buffer);

                    if (recv_state == ReceiveState::WAITING) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    if (recv_state == ReceiveState::ERROR) {
                        log<LogType::ERROR>("Lost connect to server_ptr");
                        return;
                    }

                    Message message = ReceiveQueue::parse(buffer);

					if (message.content == "Connected") {
						log<LogType::INFO>("Connected to server_ptr");
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
                send_queue.send(client_socket);
                List<char> buffer;

                const auto recv_state = receive_queue.receive(client_socket, buffer);
                if (recv_state == ReceiveState::WAITING) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                if (recv_state == ReceiveState::ERROR) {
                    log<LogType::ERROR>("Lost connect to server_ptr");
                    break;
                }

                Message message = ReceiveQueue::parse(buffer);

                try {
                    if (message.content == "Chunk version") {
                        uint8 chunk_version = std::stoi(message.arguments[0]);
                        auto cx = std::stoi(message.arguments[1]), cz = std::stoi(message.arguments[2]);

                        if (auto chunk = get_chunk(cx, cz); not chunk or chunk.value().chunk_version != chunk_version) send_queue.store({ "Get chunk data", { std::to_string(cx), std::to_string(cz) } });
                    }
                    else if (message.content == "Chunk data") {
                        // Unzip
                        if (len(buffer) < sizeof(uint32)) {
                            log<LogType::ERROR>(format{} << "Received chunk data is too small(" << len(buffer) << "). Packet might be corrupted");
                            continue;
                        }

                        std::string base64_payload = message.arguments[0];
                        String godot_base64_str = base64_payload.c_str();
                        PackedByteArray payload = Marshalls::get_singleton()->base64_to_raw(godot_base64_str);

                        if (payload.size() < sizeof(uint32)) {
                            log<LogType::ERROR>(format{} << "Received chunk data is too small(" << payload.size() << ") to contain uncompressed size. Packet might be corrupted");
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
                        std::string world_data(reinterpret_cast<byte const*>(decompressed_pba.ptr()), decompressed_pba.size());
                        std::stringstream is(world_data, std::ios::binary | std::ios::in);

                        auto cx = std::stoi(message.arguments[1]), cz = std::stoi(message.arguments[2]);
                        auto& chunk = get_or_create_chunk(cx, cz).value();
                        std::unique_lock data_lock(chunk.data_mutex);
                        chunk.clear();

                        is.read(reinterpret_cast<byte*>(&chunk.blocks[0][0][0]), (uint64)Chunk::SIZE_X * Chunk::SIZE_Y * Chunk::SIZE_Z * sizeof(BlockStorage));
                        log<LogType::VERBOSE>(format{} << chunk.blocks[0][0][0].block_id);
                        
                        is.read(reinterpret_cast<byte*>(&chunk.block_ids_size), sizeof(uint8));
                        is.read(reinterpret_cast<byte*>(&chunk.block_ids), sizeof(uint32) * 256);

                        uint8 id2block_size = 0;
                        is.read(reinterpret_cast<byte*>(&id2block_size), sizeof(uint8));
                        chunk.id2block.reserve(id2block_size);
                        for (auto j : range<uint8>(id2block_size)) {
                            uint32 global_id = 0;
                            is.read(reinterpret_cast<byte*>(&global_id), sizeof(uint32));
                            is.read(reinterpret_cast<byte*>(&chunk.id2block[global_id]), sizeof(uint8));
                        }

                        is.read(reinterpret_cast<byte*>(&chunk.tag_ids_size), sizeof(uint8));
                        is.read(reinterpret_cast<byte*>(&chunk.tag_ids), sizeof(std::pair<uint32, uint64>) * 256);

                        uint8 id2tag_size = 0;
                        is.read(reinterpret_cast<byte*>(&id2tag_size), sizeof(uint8));
                        chunk.id2tag.reserve(id2tag_size);
                        for (auto j : range<uint8>(id2tag_size)) {
                            std::pair<uint32, uint64> global_id;
                            is.read(reinterpret_cast<byte*>(&global_id.first), sizeof(uint32));
                            is.read(reinterpret_cast<byte*>(&global_id.second), sizeof(uint64));
                            is.read(reinterpret_cast<byte*>(&chunk.id2tag[global_id]), sizeof(uint8));
                        }

                        uint32 complex_size = 0;
                        is.read(reinterpret_cast<byte*>(&complex_size), sizeof(uint32));
                        chunk.complex_blocks.reserve(complex_size);
                        for (auto j : range<uint32>(complex_size)) {
                            Pos3D<uint8> key{};
                            BlockStorageFull value{};

                            is.read(reinterpret_cast<byte*>(&key.x), sizeof(uint8));
                            is.read(reinterpret_cast<byte*>(&key.y), sizeof(uint8));
                            is.read(reinterpret_cast<byte*>(&key.z), sizeof(uint8));

                            is.read(reinterpret_cast<byte*>(&value.block_id), sizeof(uint32));
                            is.read(reinterpret_cast<byte*>(&value.tag), sizeof(uint32));

                            chunk.complex_blocks.emplace(key, value);
                        }

                        is.read(reinterpret_cast<byte*>(&chunk.chunk_version), sizeof(uint8));

                        chunk.generated.store(true, std::memory_order_release);
                        chunk.dirty.store(true, std::memory_order_release);

                        Pos3D<int32> offsets[4] = { {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1} };
                        for (auto& o : offsets) {
                            auto n = get_chunk(chunk.chunk_pos.x + o.x, chunk.chunk_pos.z + o.z);
                            if (n and n.value().generated.load(std::memory_order_acquire)) n.value().dirty.store(true);
                        }

                        std::lock_guard lock(requested_chunks_mutex);
                        requested_chunks.erase({ cx, 0, cz });
                    }
                    else if (message.content == "Chunk not ready") {
                        std::lock_guard lock(requested_chunks_mutex);
                        requested_chunks.erase({ std::stoi(message.arguments[0]), 0, std::stoi(message.arguments[1]) });
                    }
                    else if (message.content == "Players data") {
                        std::stringstream is(message.arguments[0]);

                        uint64 player_count = 0;
                        is.read(reinterpret_cast<byte*>(&player_count), sizeof(uint64));

                        for (auto i : range<uint64>(player_count)) {
                            uint64 name_len = 0;
                            Str name;
                            PlayerData player_data;

                            is.read(reinterpret_cast<byte*>(&name_len), sizeof(uint64));
                            name.resize(name_len);
                            is.read(reinterpret_cast<byte*>(name.c_ptr()), name_len);
                            is.read(reinterpret_cast<byte*>(&player_data.hp), sizeof(uint8));
                            is.read(reinterpret_cast<byte*>(&player_data.pos), sizeof(Pos3D<real>));
                            is.read(reinterpret_cast<byte*>(&player_data.hotbar), sizeof(uint32) * PlayerData::HOTBAR_SIZE);

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
                    log<LogType::ERROR>(format{} << "Error processing message: " << e.what());
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            closesocket(client_socket);
            WSACleanup();
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
        const int32 px = (int32)std::floor(player_x.load() / Chunk::SIZE_X);
        const int32 pz = (int32)std::floor(player_z.load() / Chunk::SIZE_Z);

        auto get_chunk_server_or_not = +[](Main& self, Pos3D<int32> chunk_pos) { return self.server_ptr.value().get_chunk(chunk_pos.x, chunk_pos.z); };

        if (not server_ptr) {
            get_chunk_server_or_not = +[](Main& self, Pos3D<int32> chunk_pos) {
                std::lock_guard lock(self.requested_chunks_mutex);
                if (not self.requested_chunks.contains({ chunk_pos.x, 0, chunk_pos.z })) {
                    self.send_queue.store({ "Get chunk version", { std::to_string(chunk_pos.x), std::to_string(chunk_pos.z) } });
                    self.requested_chunks.insert({ chunk_pos.x, 0, chunk_pos.z });
                }
                return self.get_chunk(chunk_pos.x, chunk_pos.z);
            };
        }

        auto process_cell = [&](int32 x, int32 z) {
            Pos3D<int32> chunk_pos{ px + x, 0, pz + z };
            Ptr<Chunk> chunk_ptr = get_chunk_server_or_not(*this, chunk_pos);

            if (not chunk_ptr) return;
            Chunk& chunk = chunk_ptr.value();

            set_chunk(chunk_ptr, chunk_pos.x, chunk_pos.z);

            if (not chunk.dirty.load(std::memory_order_acquire)) return;

            {
                std::lock_guard lock(pending_jobs_mutex);
                if (pending_mesh_jobs.contains(chunk_pos)) return;
                pending_mesh_jobs.insert(chunk_pos);
            }

            mesh_pool.enqueue([this, chunk_ptr, chunk_pos]() {
                if (running.load(std::memory_order_relaxed)) {
                    Chunk& chunk = chunk_ptr.value();
                    Ptr<Chunk> neighbors[4] = {
                        get_chunk(chunk.chunk_pos.x + 1, chunk.chunk_pos.z),
                        get_chunk(chunk.chunk_pos.x - 1, chunk.chunk_pos.z),
                        get_chunk(chunk.chunk_pos.x, chunk.chunk_pos.z + 1),
                        get_chunk(chunk.chunk_pos.x, chunk.chunk_pos.z - 1)
                    };

                    chunk.generate_mesh(neighbors);

                    std::lock_guard lock(ready_chunks_queue_mutex);
                    ready_chunks_queue.push_back(chunk_ptr);
                }

                std::lock_guard lock(pending_jobs_mutex);
                pending_mesh_jobs.erase(chunk_pos);
                std::this_thread::sleep_for(1ms);
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

    void Main::create_chunk_collision(Ptr<Chunk>& chunk_ptr, PackedVector3Array const& collision_faces) {
        if (not chunk_ptr) return;

        std::shared_lock lock(chunks_mutex);
		auto& chunk = chunk_ptr.value();

        if (not chunk.mesh_instance) return;

        Ref<ArrayMesh> mesh = chunk.mesh_instance->get_mesh();
        if (mesh.is_null() or mesh->get_surface_count() == 0) return;
        if (collision_faces.size() == 0) return;

        if (collision_faces.size() % 3 != 0) {
            log<LogType::ERROR>(format{} << "Invalid faces size for collision: " << collision_faces.size());
            return;
        }

        if (not chunk.collision_body) {
            chunk.collision_body = memnew(StaticBody3D);
            chunk.mesh_instance->add_child(chunk.collision_body);
        }

        if (not chunk.collision_shape) {
            chunk.collision_shape = memnew(CollisionShape3D);
            chunk.collision_body->add_child(chunk.collision_shape);
        }

        Ref<ConcavePolygonShape3D> concave;
        concave.instantiate();
        concave->set_faces(collision_faces);

        chunk.collision_shape->set_shape(concave);
    }

    void Main::update_chunk_mesh(Ptr<Chunk>& chunk_ptr, Ref<ArrayMesh> const& mesh) {
		if (not chunk_ptr) return;
		auto& chunk = chunk_ptr.value();
        
        if (not chunk.mesh_instance) {
            MeshInstance3D* mi = memnew(MeshInstance3D);
            mi->set_position(Vector3(chunk.chunk_pos.x * Chunk::SIZE_X, 0, chunk.chunk_pos.z * Chunk::SIZE_Z));
            mi->set_material_override(world_material);
            add_child(mi);
            chunk.mesh_instance = mi;
        }

        chunk.mesh_instance->set_mesh(mesh);
    }

    void Main::unload_distant_chunks() {
        const int32 p_cx = (int32)(player_x.load() / Chunk::SIZE_X);
        const int32 p_cz = (int32)(player_z.load() / Chunk::SIZE_Z);
        const int32 unload_dist = render_distance + 4;

        List<Pos3D<int32>> chunks_to_remove;
        {
            std::shared_lock lock(chunks_mutex);
            for (auto const& [chunk_pos, chunk_ptr] : chunks) {
                int32 dx = std::abs(chunk_pos.x - p_cx);
                int32 dz = std::abs(chunk_pos.z - p_cz);
                if (dx > unload_dist or dz > unload_dist) chunks_to_remove.append(chunk_pos);
            }
        }

        int32 removed = 0;
        {
            std::unique_lock lock(chunks_mutex);
            for (auto&& chunk_pos : chunks_to_remove) {
                auto& chunk = chunks[chunk_pos].value();
                chunk.unload_mesh();
                chunks.erase(chunk_pos);
                ++removed;
            }
        }

        if (removed) log<LogType::VERBOSE>(format{} << "Queued unload for " << removed << " chunks");
    }

    Ptr<Chunk> Main::get_chunk(int32 cx, int32 cz) {
        std::shared_lock lock(chunks_mutex);
        Pos3D<int32> cpos(cx, 0, cz);

        auto it = chunks.find(cpos);
        if (it == chunks.end()) return nullptr;
        return it->second;
    }

    Ptr<Chunk> Main::get_or_create_chunk(int32 cx, int32 cz) {
        Pos3D<int32> chunk_pos{ cx, 0, cz };
        {
            std::shared_lock lock(chunks_mutex);
            auto it = chunks.find(chunk_pos);
            if (it != chunks.end()) return it->second;
        }

        std::unique_lock lock(chunks_mutex);
        auto it = chunks.find(chunk_pos);
        if (it != chunks.end()) return it->second;

        Ptr<Chunk> chunk(new Obj<Chunk>());
        chunk.value().chunk_pos = chunk_pos;
        chunks[chunk_pos].swap(chunk);
        return chunks[chunk_pos];
    }
    
    uint32 Main::get_global_block_id(int32 wx, int32 wy, int32 wz) {
        if (wy < 0 or wy >= Chunk::SIZE_Y) return BlockRegistry::get_id("Air");

        int32 cx = static_cast<int32>(std::floor((float32)wx / Chunk::SIZE_X));
        int32 cz = static_cast<int32>(std::floor((float32)wz / Chunk::SIZE_Z));
        Pos3D<int32> cpos(cx, 0, cz);

        Ptr<Chunk> chunk = get_chunk(cx, cz);
        if (not chunk) return BlockRegistry::get_id("Air");

        int32 lx = (wx % Chunk::SIZE_X + Chunk::SIZE_X) % Chunk::SIZE_X;
        int32 lz = (wz % Chunk::SIZE_Z + Chunk::SIZE_Z) % Chunk::SIZE_Z;

        return chunk.value().get_block({ (uint8)lx, (uint8)wy, (uint8)lz });
    }

    void Main::set_chunk(Ptr<Chunk> chunk, int32 cx, int32 cz) {
        std::unique_lock lock(chunks_mutex);
        Pos3D<int32> cpos(cx, 0, cz);
		chunks[cpos] = chunk;
    }

    void Main::set_global_block_id(uint32 block_id, int32 wx, int32 wy, int32 wz) {
        if (wy < 0 or wy >= Chunk::SIZE_Y) return;
        if (not server_ptr) send_queue.store({ "Set block", { std::to_string(block_id), std::to_string(wx), std::to_string(wy), std::to_string(wz) } });
        
        int32 cx = static_cast<int32>(std::floor((float32)wx / Chunk::SIZE_X));
        int32 cz = static_cast<int32>(std::floor((float32)wz / Chunk::SIZE_Z));
        Pos3D<int32> cpos(cx, 0, cz);

        Ptr<Chunk> chunk = get_chunk(cx, cz);
        if (not chunk) return;

        int32 lx = (wx % Chunk::SIZE_X + Chunk::SIZE_X) % Chunk::SIZE_X;
        int32 lz = (wz % Chunk::SIZE_Z + Chunk::SIZE_Z) % Chunk::SIZE_Z;

        chunk.value().set_block({ (uint8)lx, (uint8)wy, (uint8)lz }, block_id);
    }

    void Main::save_userdata(char const* path) {
        String real_path = ProjectSettings::get_singleton()->globalize_path(path);
        std::string std_path = real_path.utf8().get_data();

        // Tạo thư mục
        std::filesystem::create_directories(std::filesystem::path(std_path).parent_path());

        std::ofstream ofs(std_path, std::ios::binary);
        if (not ofs.is_open()) {
            log<LogType::ERROR>(format{} << "Cannot open save file: " << std_path);
            return;
        }

        std::shared_lock lock(player_mutex);
        Player* player = static_cast<Player*>(player_ptr);
        if (not player) return;

        ofs.write(reinterpret_cast<byte const*>(&full_screen), sizeof(bool));
        ofs.write(reinterpret_cast<byte const*>(&player->sensitivity), sizeof(float32));
        ofs.write(reinterpret_cast<byte const*>(&player->mouse_pitch), sizeof(float32));
        ofs.write(reinterpret_cast<byte const*>(&render_distance), sizeof(int32));
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

            ifs.read(reinterpret_cast<byte*>(&full_screen), sizeof(bool));
            ifs.read(reinterpret_cast<byte*>(&player->sensitivity), sizeof(float32));
            ifs.read(reinterpret_cast<byte*>(&player->mouse_pitch), sizeof(float32));
            ifs.read(reinterpret_cast<byte*>(&render_distance), sizeof(int32));
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
