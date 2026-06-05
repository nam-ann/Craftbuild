module;

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#undef ERROR

#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include <includes.hpp>
#include <cmath>
#include <thread>
#include <chrono>
#include <random>
#include <string>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <shared_mutex>

module game.main;

using byte = char;
import game.player;
import game.command;

namespace craftbuild {
    none Main::_ready() {
        start_log_thread();

        TagRegistry::register_tag("face");
        TagRegistry::register_tag("transparent");

        BlockRegistry::register_block<Air>          ("Air",           "");
        BlockRegistry::register_block<Grass>        ("Grass Block",   "grass_block.png");
        BlockRegistry::register_block<Dirt>         ("Dirt",          "dirt.png");
        BlockRegistry::register_block<Stone>        ("Stone",         "stone.png");
        BlockRegistry::register_block<Pebble>       ("Pebble",        "pebble.png");
        BlockRegistry::register_block<OakLog>       ("Oak Log",       "oak_log.png");
        BlockRegistry::register_block<OakPlanks>    ("Oak Planks",    "oak_planks.png");
        BlockRegistry::register_block<OakLeaves>    ("Oak Leaves",    "oak_leaves.png");
        BlockRegistry::register_block<DiamondBlock> ("Diamond Block", "diamond_block.png");
        BlockRegistry::register_block<DiamondOre>   ("Diamond Ore",   "diamond_ore.png");
        BlockRegistry::register_block<Bedrock>      ("Bedrock",       "bedrock.png");

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

        player_ptr = get_node<Player>("Player");
        if (not load_userdata()) log<LogType::WARNING>("Userdata file not found.");

        AtlasTexture::build_texture_array();
        setup_voxel_material();

        log<LogType::INFO>("Main initialized");
    }

    none Main::_process(float64 delta) {
        Player* player = static_cast<Player*>(player_ptr);
        if (not player) return;

		const auto player_pos = player->get_global_position();
        player_x.store(player_pos.x, std::memory_order_relaxed);
        player_y.store(player_pos.y, std::memory_order_relaxed);
        player_z.store(player_pos.z, std::memory_order_relaxed);

        static Pos3D<real> last_sent_pos;
        if ((player_pos - last_sent_pos) > Pos3D<real>(0.01f, 0.01f, 0.01f)) {
            if (multiplayer) {
                last_sent_pos = player_pos;
                send_queue.store({ "Update player pos", { player_name.std_str(), std::to_string(player_pos.x), std::to_string(player_pos.y), std::to_string(player_pos.z)} });
            }
            else server.value().update(player_name, player_pos);
        }

        static const int max_updates = 8;
        int updates_this_frame = 0;

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
               
            Ptr<MeshData> data = nullptr;
            {
                auto& chunk = chunk_ptr.value();
                std::lock_guard lock(chunk.mesh_mutex);
                if (chunk.pending_mesh_data) {
                    data = chunk.pending_mesh_data;
					chunk.pending_mesh_data.clear();
                    chunk.mesh_ready.store(false, std::memory_order_release);
                }
            }

            if (not data) continue;

            Ref<ArrayMesh> mesh;
            mesh.instantiate();

            if (len(data.value().vertices) != 0) {
                Array arrays;
                arrays.resize(Mesh::ARRAY_MAX);

                PackedVector3Array vertices;
                vertices.resize(len(data.value().vertices));
                memcpy(vertices.ptrw(), data.value().vertices.c_ptr(), len(data.value().vertices) * sizeof(Pos3D<float32>));

                PackedVector3Array normals;
                normals.resize(len(data.value().normals));
                memcpy(normals.ptrw(), data.value().normals.c_ptr(), len(data.value().normals) * sizeof(Pos3D<float32>));

                PackedInt32Array indices;
                indices.resize(len(data.value().indices));
                memcpy(indices.ptrw(), data.value().indices.c_ptr(), len(data.value().indices) * sizeof(int32));

                PackedVector2Array uvs;
                uvs.resize(len(data.value().uvs));
                memcpy(uvs.ptrw(), data.value().uvs.c_ptr(), len(data.value().uvs) * sizeof(Vector2));

                PackedVector2Array uvs_layer;
                uvs_layer.resize(len(data.value().uvs_layer));
                memcpy(uvs_layer.ptrw(), data.value().uvs_layer.c_ptr(), len(data.value().uvs_layer) * sizeof(Vector2));

                PackedVector3Array collision_faces;
                collision_faces.resize(len(data.value().collision_faces));
                memcpy(collision_faces.ptrw(), data.value().collision_faces.c_ptr(), len(data.value().collision_faces) * sizeof(Pos3D<float32>));

                arrays[Mesh::ARRAY_VERTEX] = vertices;
                arrays[Mesh::ARRAY_NORMAL] = normals;
                arrays[Mesh::ARRAY_INDEX] = indices;
                arrays[Mesh::ARRAY_TEX_UV] = uvs;
                arrays[Mesh::ARRAY_TEX_UV2] = uvs_layer;

                mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
                update_chunk_mesh(chunk_ptr, mesh, collision_faces);
            }

            chunk_ptr.value().collision_built.store(false, std::memory_order_release);

            updates_this_frame++;
        }

        {
            std::lock_guard lock(ready_chunks_queue_mutex);
            for (auto& chunk_ptr : deferred_chunks) ready_chunks_queue.push_back(chunk_ptr);
        }

        List<Pos3D<int>> pending_unloads;

        if (not should_remove_chunks.load(std::memory_order_acquire)) return;

        {
            std::lock_guard lock(chunks_to_remove_mutex);
            if (not chunks_to_remove) {
                should_remove_chunks.store(false, std::memory_order_release);
                return;
            }
            pending_unloads.swap(chunks_to_remove);
            should_remove_chunks.store(false, std::memory_order_release);
        }

        std::unique_lock lock(chunks_mutex);
        for (const auto& pos : pending_unloads) {
            if (not chunks.contains(pos)) continue;

            auto chunk_ptr = chunks[pos];
            if (chunk_ptr.value().mesh_instance) chunk_ptr.value().mesh_instance->queue_free();
            chunks.erase(pos);
        }
    }

    none Main::_notification(int p_what) {
        if (p_what == NOTIFICATION_EXIT_TREE) {
			running.store(false, std::memory_order_relaxed);

            if (log_thread.joinable()) log_thread.join();
            if (network_thread.joinable()) network_thread.join();
            if (scheduler_thread.joinable()) scheduler_thread.join();
            loop_cv.notify_all();

            save_userdata();
        }
    }

    none Main::init_singleplayer() {
        log<LogType::INFO>("Starting server...");
        server = new TCPServer(false);
        server.value().connect(player_name.std_str());

        start_scheduler_thread();
    }

    none Main::init_multiplayer() {
        multiplayer = true;

        start_network_thread();
        log<LogType::INFO>("Connecting to server...");
        send_queue.store({ "Connect", { player_name.std_str() } });

        start_scheduler_thread();
    }

    none Main::setup_voxel_material() {
        Ref<ShaderMaterial> mat;
        mat.instantiate();

        Ref<Shader> shader;
        shader.instantiate();
        shader->set_code(R"(
shader_type spatial;
render_mode cull_back, depth_draw_opaque, diffuse_burley;

uniform sampler2DArray u_texture_array : source_color, filter_linear_mipmap;
uniform float emissive_strength = 3.0;

varying float v_layer;

void vertex() {
    v_layer = UV2.x;
}

void fragment() {
    vec3 uvw = vec3(UV, v_layer);
    vec4 tex = texture(u_texture_array, uvw);

    if (tex.a < 0.1) {
        discard;
    }

    ALBEDO = tex.rgb;
}
        )");

        mat->set_shader(shader);
        mat->set_shader_parameter("u_texture_array", AtlasTexture::atlas_texture);

        world_material = mat;
    }

    none Main::start_log_thread() {
        if (log_thread.joinable()) return;

        auto worker = [this]() {
            ThreadRegistry::register_thread("Log Thread");
            log<LogType::INFO>("Log thread started");

            while (running.load(std::memory_order_relaxed)) {
                LogQueue::flush();
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            LogQueue::flush();
        };

        log_thread = std::thread(worker);
    }

    none Main::start_network_thread() {
        if (network_thread.joinable()) return;

        auto worker = [this]() {
            ThreadRegistry::register_thread("Network Thread");
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

            while (running.load(std::memory_order_relaxed)) {
                send_queue.send(client_socket);
                List<char> buffer;

                const auto recv_state = receive_queue.receive(client_socket, buffer);
                if (recv_state == ReceiveState::WAITING) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                if (recv_state == ReceiveState::ERROR) {
                    log<LogType::ERROR>("Lost connect to server");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    break;
                }

                Message message = ReceiveQueue::parse(buffer);

                try {
                    if (message.content == "Chunk data") {
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
                        std::string world_data((const char*)decompressed_pba.ptr(), decompressed_pba.size()); 
                        std::stringstream is(world_data, std::ios::binary | std::ios::in);

                        auto cx = std::stoi(message.arguments[1]), cz = std::stoi(message.arguments[2]);
                        auto& chunk = get_or_create_chunk(cx, cz).value();
                        std::unique_lock data_lock(chunk.data_mutex);
                        chunk.clear();

                        is.read(reinterpret_cast<byte*>(&chunk.blocks[0][0][0]), Chunk::SIZE_X * Chunk::SIZE_Y * Chunk::SIZE_Z * sizeof(BlockStorage));

                        uint8 block_ids_size = 0;
                        is.read(reinterpret_cast<byte*>(&block_ids_size), sizeof(uint8));
                        for (auto j : range<uint8>(block_ids_size)) {
                            uint8 local_id = 0;
                            uint32 global_id = 0;
                            is.read(reinterpret_cast<byte*>(&local_id), sizeof(uint8));
                            is.read(reinterpret_cast<byte*>(&global_id), sizeof(uint32));
                            chunk.block_ids[local_id] = global_id;
                        }

                        uint8 tag_ids_size = 0;
                        is.read(reinterpret_cast<byte*>(&tag_ids_size), sizeof(uint8));
                        for (auto j : range<uint8>(tag_ids_size)) {
                            uint8 local_id = 0;
                            std::pair<uint32, uint64> global_id;
                            is.read(reinterpret_cast<byte*>(&local_id), sizeof(uint8));
                            is.read(reinterpret_cast<byte*>(&global_id.first), sizeof(uint32));
                            is.read(reinterpret_cast<byte*>(&global_id.second), sizeof(uint64));
                            chunk.tag_ids[local_id] = global_id;
                        }

                        uint32 complex_size = 0;
                        is.read(reinterpret_cast<byte*>(&complex_size), sizeof(uint32));

                        auto& map = chunk.complex_blocks;
                        for (auto j : range<uint32>(complex_size)) {
                            uint8 x = 0, y = 0, z = 0;
                            uint32 block_id = 0, tag = 0;

                            is.read(reinterpret_cast<byte*>(&x), sizeof(uint8));
                            is.read(reinterpret_cast<byte*>(&y), sizeof(uint8));
                            is.read(reinterpret_cast<byte*>(&z), sizeof(uint8));

                            is.read(reinterpret_cast<byte*>(&block_id), sizeof(uint32));
                            is.read(reinterpret_cast<byte*>(&tag), sizeof(uint32));

                            Pos3D<uint8> key{ x, y, z };
                            BlockStorageFull value{ block_id, tag };

                            chunk.complex_blocks.emplace(key, value);
                        }

                        chunk.generated.store(true, std::memory_order_release);
                        chunk.dirty.store(true, std::memory_order_release);
                        chunk.mesh_ready.store(false, std::memory_order_release);
                        chunk.collision_built.store(false, std::memory_order_release);

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
                            Pos3D<real> pos;
                            uint64 name_len = 0;
                            Str name;
                            is.read(reinterpret_cast<byte*>(&name_len), sizeof(uint64));
                            name.archive(name_len);
                            is.read(reinterpret_cast<byte*>(name.c_ptr()), name_len);
                            is.read(reinterpret_cast<byte*>(&pos), sizeof(Pos3D<real>));
                        }
                    }
                    else if (message.content == "Chat response") call_deferred("emit_signal", "chat_output", message.arguments[0].c_str());
                }
                catch (const std::exception& e) {
                    log<LogType::ERROR>(format{} << "Error processing message: " << e.what());
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            closesocket(client_socket);
            WSACleanup();
        };

        network_thread = std::thread(worker);
    }

    none Main::start_scheduler_thread() {
        scheduler_thread = std::thread([this]() {
            ThreadRegistry::register_thread("Scheduler Thread");
            log<LogType::INFO>("Scheduler thread started");

            auto last_unload_time = std::chrono::high_resolution_clock::now();
            while (running.load(std::memory_order_relaxed)) {
                submit_jobs();

                auto now = std::chrono::high_resolution_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_unload_time).count() >= 5) {
                    unload_distant_chunks((int)(player_x.load() / Chunk::SIZE_X), (int)(player_z.load() / Chunk::SIZE_Z));
                    last_unload_time = now;
                }

                std::unique_lock<std::mutex> lock(loop_mutex);
                loop_cv.wait_for(lock, std::chrono::milliseconds(sleep_time_cpu));
            }
        });
    }

    none Main::submit_jobs() {
        const int px = (int)std::floor(player_x.load() / Chunk::SIZE_X);
        const int pz = (int)std::floor(player_z.load() / Chunk::SIZE_Z);
        for (auto r : range<int>(render_distance + 1)) {
            for (auto x : range<int>(-r, r + 1)) {
                for (auto z : range<int>(-r, r + 1)) {
                    if (std::abs(x) != r and std::abs(z) != r) continue;

                    Pos3D<int32> chunk_pos{ px + x, 0, pz + z };

                    Ptr<Chunk> chunk_ptr;
                    if (multiplayer) {
                        {
                            std::lock_guard lock(requested_chunks_mutex);
                            if (requested_chunks.contains({ chunk_pos.x, 0, chunk_pos.z })) continue;
                            requested_chunks.insert({ chunk_pos.x, 0, chunk_pos.z });
                        }
                        send_queue.store({ "Get chunk data", { std::to_string(chunk_pos.x), std::to_string(chunk_pos.z) } });
                        chunk_ptr = get_chunk(chunk_pos.x, chunk_pos.z);
                    }
                    else chunk_ptr = server.value().get_chunk(chunk_pos.x, chunk_pos.z);

                    if (not chunk_ptr) continue;
                    Chunk& chunk = chunk_ptr.value();

                    set_chunk(chunk_ptr, chunk_pos.x, chunk_pos.z);

                    if (not chunk.dirty.load(std::memory_order_acquire) or chunk.mesh_ready.load(std::memory_order_acquire)) continue;
                    
                    {
                        std::lock_guard lock(pending_jobs_mutex);
                        if (pending_mesh_jobs.contains(chunk_pos)) continue;
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
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    });
                }
            }
        }
    }

    none Main::create_chunk_collision(Ptr<Chunk> chunk, const PackedVector3Array& collision_faces) {
		std::shared_lock lock(chunks_mutex);

        if (not chunk.value().mesh_instance or chunk.value().collision_built.load(std::memory_order_relaxed)) return;
        
        for (auto i : range<int32>(chunk.value().mesh_instance->get_child_count() - 1, -1)) {
            Node* child = chunk.value().mesh_instance->get_child(i);
            if (Object::cast_to<StaticBody3D>(child)) {
                chunk.value().mesh_instance->remove_child(child);
                child->queue_free();
            }
        }

        Ref<ArrayMesh> mesh = chunk.value().mesh_instance->get_mesh();
        if (mesh.is_null() or mesh->get_surface_count() == 0) return;
        if (collision_faces.size() == 0) return;

        if (collision_faces.size() % 3 != 0) {
            log<LogType::ERROR>(format{} << "Invalid faces size for collision: " << collision_faces.size());
            return;
        }

        StaticBody3D* static_body = memnew(StaticBody3D);
        CollisionShape3D* col_shape = memnew(CollisionShape3D);
        Ref<ConcavePolygonShape3D> concave;
        concave.instantiate();

        concave->set_faces(collision_faces);
        col_shape->set_shape(concave);
        static_body->add_child(col_shape);

        chunk.value().mesh_instance->add_child(static_body);
        chunk.value().collision_built.store(true, std::memory_order_release);
    }
    
    none Main::update_chunk_mesh(Ptr<Chunk> chunk, Ref<ArrayMesh> mesh, PackedVector3Array& collision_faces) {
        if (not chunk.value().mesh_instance) {
            MeshInstance3D* mi = memnew(MeshInstance3D);
            mi->set_position(Vector3(chunk.value().chunk_pos.x * Chunk::SIZE_X, 0,chunk.value().chunk_pos.z * Chunk::SIZE_Z));
            mi->set_material_override(world_material);
            add_child(mi);
            chunk.value().mesh_instance = mi;
        }

        chunk.value().mesh_instance->set_mesh(mesh);
		create_chunk_collision(chunk, collision_faces);
    }

    none Main::unload_distant_chunks(int p_cx, int p_cz) {
        const int unload_dist = render_distance + 4;
        List<Pos3D<int>> chunks_to_remove;

        {
            std::shared_lock lock(chunks_mutex);
            for (const auto& E : chunks) {
                int dx = std::abs(E.first.x - p_cx);
                int dz = std::abs(E.first.z - p_cz);
                if (dx > unload_dist or dz > unload_dist) {
                    chunks_to_remove.append(E.first);
                }
            }
        }

        {
            std::lock_guard lock(chunks_to_remove_mutex);
            this->chunks_to_remove += chunks_to_remove;
        }
        should_remove_chunks.store(true, std::memory_order_release);

        if (chunks_to_remove) log<LogType::VERBOSE>(format{} << "Queued unload for " << len(chunks_to_remove) << " chunks.");
    }

    Ptr<Chunk> Main::get_chunk(int cx, int cz) {
        std::shared_lock lock(chunks_mutex);
        Pos3D<int> cpos(cx, 0, cz);

        auto it = chunks.find(cpos);
        if (it == chunks.end()) return nullptr;
        return it->second;
    }

    Ptr<Chunk> Main::get_or_create_chunk(int cx, int cz) {
        Pos3D<int> chunk_pos{ cx, 0, cz };
        {
            std::shared_lock lock(chunks_mutex);
            auto it = chunks.find(chunk_pos);
            if (it != chunks.end()) return it->second;
        }

        std::unique_lock lock(chunks_mutex);
        auto it = chunks.find(chunk_pos);
        if (it != chunks.end()) return it->second;

        Ptr<Chunk> chunk(new Chunk());
        chunk.value().chunk_pos = chunk_pos;
        chunks[chunk_pos] = chunk;
        return chunk;
    }
    
    uint32 Main::get_global_block_id(int wx, int wy, int wz) {
        if (wy < 0 or wy >= Chunk::SIZE_Y) return BlockRegistry::get_id("Air");

        int cx = static_cast<int>(std::floor((float32)wx / Chunk::SIZE_X));
        int cz = static_cast<int>(std::floor((float32)wz / Chunk::SIZE_Z));
        Pos3D<int> cpos(cx, 0, cz);

        Ptr<Chunk> chunk = get_chunk(cx, cz);
        if (not chunk) return BlockRegistry::get_id("Air");

        int lx = (wx % Chunk::SIZE_X + Chunk::SIZE_X) % Chunk::SIZE_X;
        int lz = (wz % Chunk::SIZE_Z + Chunk::SIZE_Z) % Chunk::SIZE_Z;

        return chunk.value().get_block<false>({ (uint8)lx, (uint8)wy, (uint8)lz });
    }

    none Main::set_chunk(Ptr<Chunk> chunk, int cx, int cz) {
        std::unique_lock lock(chunks_mutex);
        Pos3D<int> cpos(cx, 0, cz);
		chunks[cpos] = chunk;
    }

    none Main::set_global_block_id(uint32 block_id, int wx, int wy, int wz) {
        if (wy < 0 or wy >= Chunk::SIZE_Y) return;

        int cx = static_cast<int>(std::floor((float32)wx / Chunk::SIZE_X));
        int cz = static_cast<int>(std::floor((float32)wz / Chunk::SIZE_Z));
        Pos3D<int> cpos(cx, 0, cz);

        Ptr<Chunk> chunk = get_chunk(cx, cz);
        if (not chunk) return;

        int lx = (wx % Chunk::SIZE_X + Chunk::SIZE_X) % Chunk::SIZE_X;
        int lz = (wz % Chunk::SIZE_Z + Chunk::SIZE_Z) % Chunk::SIZE_Z;

        chunk.value().set_block({ (uint8)lx, (uint8)wy, (uint8)lz }, block_id);
    }

    none Main::save_userdata(const char* path) {
        String real_path = ProjectSettings::get_singleton()->globalize_path(path);
        std::string std_path = real_path.utf8().get_data();

        // Tạo thư mục
        std::filesystem::create_directories(std::filesystem::path(std_path).parent_path());

        std::ofstream ofs(std_path, std::ios::binary);
        if (not ofs.is_open()) {
            log<LogType::ERROR>(format{} << "Cannot open save file: " << std_path);
            return;
        }

        Player* player = static_cast<Player*>(player_ptr);
        if (not player) return;

        ofs.write(reinterpret_cast<const byte*>(&full_screen), sizeof(bool));
        ofs.write(reinterpret_cast<const byte*>(&player->sensitivity), sizeof(float32));
        ofs.write(reinterpret_cast<const byte*>(&player->mouse_pitch), sizeof(float32));
        ofs.write(reinterpret_cast<const byte*>(&render_distance), sizeof(int));
    }

    bool Main::load_userdata(const char* path) {
        String real_path = ProjectSettings::get_singleton()->globalize_path(path);
        std::string std_path = real_path.utf8().get_data();

        std::ifstream ifs(std_path, std::ios::binary);
        if (not ifs.is_open()) return false;

        Player* player = static_cast<Player*>(player_ptr);
        if (not player) return false;

        ifs.read(reinterpret_cast<byte*>(&full_screen), sizeof(bool));
        ifs.read(reinterpret_cast<byte*>(&player->sensitivity), sizeof(float32));
        ifs.read(reinterpret_cast<byte*>(&player->mouse_pitch), sizeof(float32));
        ifs.read(reinterpret_cast<byte*>(&render_distance), sizeof(int));

		log<LogType::INFO>("Userdata loaded");

        return true;
    }

    none Main::pause() {
        Input* input = Input::get_singleton();
        input->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
        pausing.store(true, std::memory_order_relaxed);
    }
    none Main::resume() {
        Input* input = Input::get_singleton();
        input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
        pausing.store(false, std::memory_order_relaxed);
    }

    none Main::start_chat() {
        Input* input = Input::get_singleton();
        input->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
        chatting.store(true, std::memory_order_relaxed);
    }

    none Main::chat(const String msg) {
        if (multiplayer) send_queue.store({ "Chat", { (std::string)msg.utf8() } });
        else {
            Str output = server.value().chat((std::string)msg.utf8());
			emit_signal("chat_output", output.std_str().c_str());
        }
        Input* input = Input::get_singleton();
        input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
        chatting.store(false, std::memory_order_relaxed);
    }

    none Main::set_seed_and_world_name(int32 seed, const String name) {
        if (multiplayer) send_queue.store({ "Set seed and world name", { std::to_string(seed), (std::string)name.utf8() } });
        else server.value().set_seed_and_world_name(seed, (std::string)name.utf8());
    }

    none Main::set_render_distance(int32 rd) {
        if (multiplayer) send_queue.store({ "Set render distance", { std::to_string(rd) } });
        else server.value().set_render_distance(rd);
    }

    none Main::set_sleep_time_cpu(int32 stc) {
        if (multiplayer) send_queue.store({ "Set sleep time CPU", { std::to_string(stc) } });
        else server.value().set_sleep_time_cpu(stc);
    }
    
    none Main::_bind_methods() {
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
        ClassDB::bind_method(D_METHOD("set_sleep_time_cpu", "stc"), &Main::set_sleep_time_cpu);
    }
}
