module;

#include <defs.hpp>

NO_WARNING
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#undef ERROR

#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
DO_WARNING

module game.server;

import game.command;

using namespace std::chrono_literals;

namespace craftbuild {
    void TCPServer::_get_refs(std::vector<GCObject*>&refs) {
		std::shared_lock lock(chunks_mutex);
		for (auto const& [_, chunk_ptr] : chunks) {
			if (chunk_ptr) refs.push_back(chunk_ptr.object());
		}
    }

    TCPServer::TCPServer() {
        command_ptr = new CommandInterpreter(this);

        noise.instantiate();
        noise->set_noise_type(FastNoiseLite::TYPE_SIMPLEX);
        noise->set_frequency(0.01f);

        if (not load_world(format{} << "user://game/saves/" << world_name)) {
            log<LogType::WARNING>("Save file not found, starting new world.");
            if (world_seed.load(std::memory_order_acquire) == 0) {
                std::mt19937 generator;
                std::uniform_int_distribution<int32> distribution;
                world_seed.store(distribution(generator), std::memory_order_release);
            }
        }
        noise->set_seed(world_seed.load(std::memory_order_acquire));

        log<LogType::VERBOSE>("Assets loaded");

        start_redstone_thread();
        start_scheduler_thread();

        log<LogType::INFO>("Server initialized");
    }

    TCPServer::~TCPServer() {
		running.store(false, std::memory_order_relaxed);

		if (redstone_thread.joinable()) redstone_thread.join();
		if (scheduler_thread.joinable()) scheduler_thread.join();
        loop_cv.notify_all();

        save_world(format{} << "user://game/saves/" << world_name);
    }

    void TCPServer::disconnect(Str const& player_name) {
        if (not players.contains(player_name)) return;

        std::unique_lock lock(player_mutex);
        online_players.erase(player_name);

        {
            std::lock_guard lock(current_player_mutex);
            current_player = online_players.begin();
        }

        log<LogType::INFO>(format{} << "Player disconnected: " << player_name);
    }

	void TCPServer::connect(Str const& player_name) {
        std::unique_lock lock(player_mutex);
        online_players[player_name];

        {
            std::lock_guard lock(current_player_mutex);
            current_player = online_players.begin();
        }

        if (players.contains(player_name)) return;
        players[player_name] = PlayerData{ .name = player_name, .pos = {0, 0, 0} };

		log<LogType::INFO>(format{} << "Player connected: " << player_name);
	}

	void TCPServer::update(Str const& player_name, Pos3D<real> const& new_pos) {
		std::unique_lock lock(player_mutex);
        players[player_name].pos = new_pos;
	}

    void TCPServer::start_redstone_thread() {
        if (redstone_thread.joinable()) return;

        auto worker = [this]() {
            ThreadRegistry::register_thread("Redstone");
            log<LogType::INFO>("Redstone thread started");

            while (running.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(10ms);
            }
        };

        redstone_thread = std::thread(worker);
    }

    void TCPServer::start_scheduler_thread() {
        scheduler_thread = std::thread([this]() {
            ThreadRegistry::register_thread("Terrain");
            log<LogType::INFO>("Terrain thread started");

            auto last_unload_time = std::chrono::high_resolution_clock::now();
            current_player = online_players.begin();
            while (running.load(std::memory_order_relaxed)) {
                {
                    std::shared_lock lock(player_mutex);
                    if (online_players.empty()) {
                        std::this_thread::sleep_for(500ms);
                        continue;
                    }
                }

                if (auto now = std::chrono::high_resolution_clock::now(); std::chrono::duration_cast<std::chrono::seconds>(now - last_unload_time).count() >= 5) {
                    unload_distant_chunks();
                    last_unload_time = now;
                }
                    
                {
                    std::lock_guard lock1(current_player_mutex);
                    Pos3D<real> pos;
                    {
                        std::shared_lock lock(player_mutex);
                        pos = players[current_player->first].pos;
                    }

                    submit_jobs(pos);

                    std::shared_lock lock2(player_mutex);
                    if (++current_player == online_players.end()) current_player = online_players.begin();
                }

                std::unique_lock<std::mutex> lock(loop_mutex);
                loop_cv.wait_for(lock, std::chrono::milliseconds(cpu_sleep_time));
            }
        });
    }

    void TCPServer::submit_jobs(Pos3D<real> const& player) {
        int32 px = static_cast<int32>(std::floor(player.x / Chunk::SIZE_X));
        int32 pz = static_cast<int32>(std::floor(player.z / Chunk::SIZE_Z));

        auto get_or_load_or_create_chunk = [this](int32 cx, int32 cz) -> Ptr<Chunk> {
			if (auto chunk_ptr = get_or_load_chunk(cx, cz)) return chunk_ptr;
            return get_or_create_chunk(cx, cz);
        };

        auto process_cell = [&](int32 x, int32 z) {
            Pos3D<int32> chunk_pos{ px + x, 0, pz + z };
            auto chunk_ptr = get_or_load_or_create_chunk(chunk_pos.x, chunk_pos.z);

            if (chunk_ptr.value().generated.load(std::memory_order_acquire)) return;

            {
                std::lock_guard lock(pending_jobs_mutex);
                if (pending_terrain_jobs.contains(chunk_pos)) return;
                pending_terrain_jobs.insert(chunk_pos);
            }

            terrain_pool.enqueue([this, chunk_ptr]() {
                auto& chunk = chunk_ptr.value();

                if (running.load(std::memory_order_relaxed)) {
                    chunk.generate_terrain(world_seed.load(), noise);

                    Pos3D<int32> offsets[4] = { {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1} };
                    for (auto& o : offsets) {
                        auto n_ptr = get_or_load_chunk(chunk.chunk_pos.x + o.x, chunk.chunk_pos.z + o.z);
                        if (not n_ptr) continue;

                        auto& n = n_ptr.value();
                        if (n.generated.load(std::memory_order_acquire)) {
                            n.dirty.store(true);
                            ++n.chunk_version;
                        }
                    }
                }

                std::lock_guard lock(pending_jobs_mutex);
                pending_terrain_jobs.erase(chunk.chunk_pos);
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

    std::string TCPServer::serialize_players() {
        std::stringstream os;

        {
            std::shared_lock lock(player_mutex);
            uint64 player_count = online_players.size();
            os.write(reinterpret_cast<char const*>(&player_count), sizeof(uint64));

            for (auto const& [player_name, _] : online_players) {
                auto const& player_data = players[player_name];

                uint64 name_len = len(player_name);
                os.write(reinterpret_cast<char const*>(&name_len), sizeof(uint64));
                os.write(reinterpret_cast<char const*>(player_name.c_ptr()), name_len);
                os.write(reinterpret_cast<char const*>(&player_data.hp), sizeof(uint8));
                os.write(reinterpret_cast<char const*>(&player_data.pos), sizeof(Pos3D<real>));
                os.write(reinterpret_cast<char const*>(&player_data.hotbar), sizeof(uint32) * PlayerData::HOTBAR_SIZE);
            }
        }

        return os.str();
    }

    std::string TCPServer::serialize_chunk(int32 cx, int32 cz) {
        std::stringstream os(std::ios::binary | std::ios::out);

		// Get & serialize chunk data
        Ptr<Chunk> chunk_ptr = get_chunk(cx, cz);
        if (not chunk_ptr) return "";
        auto& chunk = chunk_ptr.value();

        std::shared_lock data_lock(chunk.data_mutex);

        os.write(reinterpret_cast<char const*>(&chunk.blocks[0][0][0]), (uint64)Chunk::SIZE_X * Chunk::SIZE_Y * Chunk::SIZE_Z * sizeof(BlockStorage));
        
        os.write(reinterpret_cast<char const*>(&chunk.block_ids_size), sizeof(uint8));
        os.write(reinterpret_cast<char const*>(&chunk.block_ids[0]), sizeof(uint32) * 256);

        uint8 id2block_size = static_cast<uint8>(chunk.id2block.size());
        os.write(reinterpret_cast<char const*>(&id2block_size), sizeof(uint8));
        for (auto const& [global_id, local_id] : chunk.id2block) {
            os.write(reinterpret_cast<char const*>(&global_id), sizeof(uint32));
            os.write(reinterpret_cast<char const*>(&local_id), sizeof(uint8));
        }

        os.write(reinterpret_cast<char const*>(&chunk.tag_ids_size), sizeof(uint8));
        os.write(reinterpret_cast<char const*>(&chunk.tag_ids[0]), sizeof(std::pair<uint32, uint64>) * 256);

        uint8 id2tag_size = static_cast<uint8>(chunk.id2tag.size());
        os.write(reinterpret_cast<char const*>(&id2tag_size), sizeof(uint8));
        for (auto const& [global_id, local_id] : chunk.id2tag) {
            os.write(reinterpret_cast<char const*>(&global_id.first), sizeof(uint32));
            os.write(reinterpret_cast<char const*>(&global_id.second), sizeof(uint64));
            os.write(reinterpret_cast<char const*>(&local_id), sizeof(uint8));
        }

        uint32 complex_size = static_cast<uint32>(chunk.complex_blocks.size());
        os.write(reinterpret_cast<char const*>(&complex_size), sizeof(uint32));

        for (auto const& pair : chunk.complex_blocks) {
            os.write(reinterpret_cast<char const*>(&pair.first.x), sizeof(uint8));
            os.write(reinterpret_cast<char const*>(&pair.first.y), sizeof(uint8));
            os.write(reinterpret_cast<char const*>(&pair.first.z), sizeof(uint8));

            os.write(reinterpret_cast<char const*>(&pair.second.block_id), sizeof(uint32));
            os.write(reinterpret_cast<char const*>(&pair.second.tag), sizeof(uint32));
        }

        os.write(reinterpret_cast<char const*>(&chunk.chunk_version), sizeof(uint8));

        // Zip
        std::string raw_data = os.str();
        uint32 uncompressed_size = static_cast<uint32>(raw_data.size());

        PackedByteArray pba;
        pba.resize(uncompressed_size);
        memcpy(pba.ptrw(), raw_data.data(), uncompressed_size);

        PackedByteArray compressed_pba = pba.compress(FileAccess::COMPRESSION_ZSTD);

        PackedByteArray final_payload;
        final_payload.resize(sizeof(uint32) + compressed_pba.size());

        memcpy(final_payload.ptrw(), &uncompressed_size, sizeof(uint32));
        memcpy(final_payload.ptrw() + sizeof(uint32), compressed_pba.ptr(), compressed_pba.size());
        
        String base64_godot_str = Marshalls::get_singleton()->raw_to_base64(final_payload);

        return (std::string)base64_godot_str.utf8();
    }

    Ptr<Chunk> TCPServer::get_chunk(int32 cx, int32 cz) {
        Pos3D<int32> cpos(cx, 0, cz);

        std::shared_lock lock(chunks_mutex);
        auto it = chunks.find(cpos);

        if (it == chunks.end()) return nullptr;
        return it->second;
    }

    Ptr<Chunk> TCPServer::get_or_load_chunk(int32 cx, int32 cz) {
        if (auto chunk_ptr = get_chunk(cx, cz)) return chunk_ptr;

        const int32 rx = (cx >= 0) ? (cx / 16) : ((cx - 15) / 16);
        const int32 rz = (cz >= 0) ? (cz / 16) : ((cz - 15) / 16);

        const Str path = format{} << "user://game/saves/" << world_name;
        const String real_path = ProjectSettings::get_singleton()->globalize_path((path + "/regions/" + Str(rx) + "_" + Str(rz) + ".cbregion").std_str().c_str());
        const std::string std_path = (std::string)real_path.utf8();

        const auto chunk_pos = Pos3D<int32>{ cx, 0, cz };

        if (std::filesystem::exists(std_path)) {
            load_region(path, rx, rz);

            std::shared_lock lock(chunks_mutex);
            auto it_loaded = chunks.find(chunk_pos);
            if (it_loaded != chunks.end()) return it_loaded->second;
        }

        return nullptr;
    };

    Ptr<Chunk> TCPServer::get_or_create_chunk(int32 cx, int32 cz) {
        Pos3D<int32> chunk_pos{ cx, 0, cz };
        {
            std::shared_lock lock(chunks_mutex);
            auto it = chunks.find(chunk_pos);
            if (it != chunks.end()) return it->second;
        }

        std::unique_lock lock(chunks_mutex);

        Ptr<Chunk> chunk = new Obj<Chunk>();
        chunk.value().chunk_pos = chunk_pos;
        chunks[chunk_pos].swap(chunk);

        return chunks[chunk_pos];
    }

    uint32 TCPServer::get_global_block_id(int32 wx, int32 wy, int32 wz) {
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

    void TCPServer::set_global_block_id(uint32 block_id, int32 wx, int32 wy, int32 wz) {
        if (wy < 0 or wy >= Chunk::SIZE_Y) return;

        int32 cx = static_cast<int32>(std::floor((float32)wx / Chunk::SIZE_X));
        int32 cz = static_cast<int32>(std::floor((float32)wz / Chunk::SIZE_Z));
        Pos3D<int32> cpos(cx, 0, cz);

        Ptr<Chunk> chunk = get_chunk(cx, cz);
        if (not chunk) return;

        int32 lx = (wx % Chunk::SIZE_X + Chunk::SIZE_X) % Chunk::SIZE_X;
        int32 lz = (wz % Chunk::SIZE_Z + Chunk::SIZE_Z) % Chunk::SIZE_Z;

        chunk.value().set_block({ (uint8)lx, (uint8)wy, (uint8)lz }, block_id);
    }

    void TCPServer::unload_distant_chunks() {
        const int32 unload_dist = render_distance + 4;
        Set<Pos3D<int32>> still_viewing_chunks;

        {
            std::shared_lock lock(chunks_mutex);
            for (auto const& [chunk_pos, _] : chunks) {
                std::shared_lock lock(player_mutex);
                for (auto const& [player_name, _] : online_players) {
                    auto& player_pos = players[player_name].pos;
                    int32 dx = std::abs(chunk_pos.x - (int32)std::floor(player_pos.x / Chunk::SIZE_X));
                    int32 dz = std::abs(chunk_pos.z - (int32)std::floor(player_pos.z / Chunk::SIZE_Z));
                    if (dx <= unload_dist and dz <= unload_dist) still_viewing_chunks.insert(chunk_pos);
                }
            }
        }

        Set<Pos3D<int32>> regions_to_save;

        {
            std::unique_lock lock(chunks_mutex);
            for (auto it = chunks.begin(); it != chunks.end();) {
                auto const& chunk_pos = it->first;

                if (still_viewing_chunks.find(chunk_pos) == still_viewing_chunks.end()) {
                    int32 rx = (chunk_pos.x >= 0) ? (chunk_pos.x / 16) : ((chunk_pos.x - 15) / 16);
                    int32 rz = (chunk_pos.z >= 0) ? (chunk_pos.z / 16) : ((chunk_pos.z - 15) / 16);

                    regions_to_save.insert({ rx, 0, rz });
                    it = chunks.erase(it);
                }
                else ++it;
            }
        }

        for (auto const& [rx, _, rz] : regions_to_save) {
            save_region(format{} << "user://game/saves/" << world_name, rx, rz);
        }
    }

    void TCPServer::set_seed_and_world_name(int32 seed, Str const& name) {
		world_seed.store(seed, std::memory_order_release);
		noise->set_seed(seed);
		world_name = name;
    }

    void TCPServer::set_render_distance(int32 rd) { render_distance = rd; }
    void TCPServer::set_cpu_sleep_time(int32 stc) { cpu_sleep_time = stc; }

    Str TCPServer::chat(Str const& msg) {
        if (msg) {
            std::string _msg = msg.std_str();
            log<LogType::NORMAL>(format{} << "[Player] " << _msg);
            if (_msg.starts_with("/")) {
                CommandInterpreter* interpreter = static_cast<CommandInterpreter*>(command_ptr);
                return interpreter->execute_command(_msg.erase(0, 1)).std_str().c_str();
            }
            else return msg;
        }
        return "";
    }

    void TCPServer::save_world(Str const& path) {
        String real_path = ProjectSettings::get_singleton()->globalize_path((path + "/" + world_name + ".cbworld").std_str().c_str());
        std::string std_path = (std::string)real_path.utf8();

        // Tạo thư mục
        std::filesystem::create_directories(std::filesystem::path(std_path).parent_path());

        std::ofstream ofs(std_path, std::ios::binary);
        if (not ofs.is_open()) {
            log<LogType::ERROR>(format{} << "Cannot open save file: " << std_path);
            return;
        }

        log<LogType::INFO>("Saving world...");

        uint64 version_len = strlen(version);
        ofs.write(reinterpret_cast<char const*>(&version_len), sizeof(uint64));
        ofs.write(version, sizeof(char) * version_len);

        uint32 seed = world_seed.load(std::memory_order_release);
        ofs.write(reinterpret_cast<char const*>(&seed), sizeof(uint32));

        {
            std::shared_lock lock(player_mutex);
            const uint64 player_count = players.size();
            ofs.write(reinterpret_cast<char const*>(&player_count), sizeof(uint64));

            for (auto const& [player_name, player_data] : players) {
                const uint64 player_name_len = len(player_name);
                ofs.write(reinterpret_cast<char const*>(&player_name_len), sizeof(uint64));
                ofs.write(reinterpret_cast<char const*>(player_name.c_ptr()), player_name_len);
                ofs.write(reinterpret_cast<char const*>(&player_data.hp), sizeof(uint8));
                ofs.write(reinterpret_cast<char const*>(&player_data.pos), sizeof(Pos3D<real>));
                ofs.write(reinterpret_cast<char const*>(&player_data.hotbar), sizeof(uint32) * PlayerData::HOTBAR_SIZE);
            }
        }

        ofs.close();

        Set<Pos3D<int32>> regions_to_save;

        {
			std::shared_lock lock(chunks_mutex);
            for (auto const& [chunk_pos, _] : chunks) {
                int32 rx = (chunk_pos.x >= 0) ? (chunk_pos.x / 16) : ((chunk_pos.x - 15) / 16);
                int32 rz = (chunk_pos.z >= 0) ? (chunk_pos.z / 16) : ((chunk_pos.z - 15) / 16);
                regions_to_save.insert({ rx, 0, rz });
            }
        }

        for (auto const& [rx, _, rz] : regions_to_save) save_region(path, rx, rz);

        log<LogType::INFO>(format{} << "World saved!");
    }

    bool TCPServer::load_world(Str const& path) {
        String real_path = ProjectSettings::get_singleton()->globalize_path((path + "/" + world_name + ".cbworld").std_str().c_str());
        std::string std_path = (std::string)real_path.utf8();

        std::ifstream ifs(std_path, std::ios::binary);
        if (not ifs.is_open()) return false;

        log<LogType::INFO>("Loading world...");

        uint64 version_len = 0;
        ifs.read(reinterpret_cast<char*>(&version_len), sizeof(uint64));

        std::string current_version(version_len, '\0');
        ifs.read(current_version.data(), sizeof(char) * version_len);
        if (current_version != version) {
            log<LogType::WARNING>(format{} << "Save version" << "(" << current_version << ")" << " mismatch with current version(" << version << ")");
			log<LogType::WARNING>("This game's world loader doesn't support Data Migration. World data might get damaged and cause crashes");
        }

        uint32 seed = 0;
        ifs.read(reinterpret_cast<char*>(&seed), sizeof(uint32));
        noise->set_seed(seed);
        world_seed.store(static_cast<int32>(seed), std::memory_order_release);

        uint64 player_count = 0;
        ifs.read(reinterpret_cast<char*>(&player_count), sizeof(uint64));

        {
            std::unique_lock lock(player_mutex);
            for (auto i : range<uint64>(player_count)) {
                Str player_name;
                uint64 player_name_len = 0;
                ifs.read(reinterpret_cast<char*>(&player_name_len), sizeof(uint64));
                player_name.resize(player_name_len);
                ifs.read(reinterpret_cast<char*>(player_name.c_ptr()), player_name_len);
                ifs.read(reinterpret_cast<char*>(&players[player_name].hp), sizeof(uint8));
                ifs.read(reinterpret_cast<char*>(&players[player_name].pos), sizeof(Pos3D<real>));
                ifs.read(reinterpret_cast<char*>(&players[player_name].hotbar), sizeof(uint32) * PlayerData::HOTBAR_SIZE);
            }
        }
        
        log<LogType::INFO>("World loaded successfully!");
        return true;
    }

    void TCPServer::save_region(Str const& path, int32 rx, int32 rz) {
        String real_path = ProjectSettings::get_singleton()->globalize_path((path + "/regions/" + Str(rx) + "_" + Str(rz) + ".cbregion").std_str().c_str());
        std::string std_path = (std::string)real_path.utf8();

        std::filesystem::create_directories(std::filesystem::path(std_path).parent_path());

        std::ofstream ofs(std_path, std::ios::binary);
        if (not ofs.is_open()) {
            log<LogType::ERROR>(format{} << "Cannot open save file: " << std_path);
            return;
        }

        const int32 cx = rx * 16;
        const int32 cz = rz * 16;

		for (int32 x : range<int32>(cx, cx + 16)) {
			for (int32 z : range<int32>(cz, cz + 16)) {
				auto chunk_ptr = get_chunk(x, z);

                const bool chunk_exists = chunk_ptr and chunk_ptr.value().generated.load(std::memory_order_acquire);
			    ofs.write(reinterpret_cast<char const*>(&chunk_exists), sizeof(bool));
                
                if (not chunk_exists) continue;

                auto& chunk = chunk_ptr.value();
                std::shared_lock data_lock(chunk.data_mutex);

                ofs.write(reinterpret_cast<char const*>(&chunk.blocks[0][0][0]), Chunk::SIZE_X * Chunk::SIZE_Y * Chunk::SIZE_Z * sizeof(BlockStorage));

                ofs.write(reinterpret_cast<char const*>(&chunk.block_ids_size), sizeof(uint8));
                ofs.write(reinterpret_cast<char const*>(&chunk.block_ids[0]), sizeof(uint32) * 256);

                uint8 id2block_size = static_cast<uint8>(chunk.id2block.size());
                ofs.write(reinterpret_cast<char const*>(&id2block_size), sizeof(uint8));
                for (auto const& [global_id, local_id] : chunk.id2block) {
                    ofs.write(reinterpret_cast<char const*>(&global_id), sizeof(uint32));
                    ofs.write(reinterpret_cast<char const*>(&local_id), sizeof(uint8));
                }

                ofs.write(reinterpret_cast<char const*>(&chunk.tag_ids_size), sizeof(uint8));
                ofs.write(reinterpret_cast<char const*>(&chunk.tag_ids[0]), sizeof(std::pair<uint32, uint64>) * 256);

                uint8 id2tag_size = static_cast<uint8>(chunk.id2tag.size());
                ofs.write(reinterpret_cast<char const*>(&id2tag_size), sizeof(uint8));
                for (auto const& [global_id, local_id] : chunk.id2tag) {
                    ofs.write(reinterpret_cast<char const*>(&global_id.first), sizeof(uint32));
                    ofs.write(reinterpret_cast<char const*>(&global_id.second), sizeof(uint64));
                    ofs.write(reinterpret_cast<char const*>(&local_id), sizeof(uint8));
                }

                uint32 complex_size = static_cast<uint32>(chunk.complex_blocks.size());
                ofs.write(reinterpret_cast<char const*>(&complex_size), sizeof(uint32));

                for (auto const& pair : chunk.complex_blocks) {
                    ofs.write(reinterpret_cast<char const*>(&pair.first.x), sizeof(uint8));
                    ofs.write(reinterpret_cast<char const*>(&pair.first.y), sizeof(uint8));
                    ofs.write(reinterpret_cast<char const*>(&pair.first.z), sizeof(uint8));

                    ofs.write(reinterpret_cast<char const*>(&pair.second.block_id), sizeof(uint32));
                    ofs.write(reinterpret_cast<char const*>(&pair.second.tag), sizeof(uint32));
                }
			}
		}
    }

    bool TCPServer::load_region(Str const& path, int32 rx, int32 rz) {
        String real_path = ProjectSettings::get_singleton()->globalize_path((path + "/regions/" + Str(rx) + "_" + Str(rz) + ".cbregion").std_str().c_str());
        std::string std_path = (std::string)real_path.utf8();

        std::ifstream ifs(std_path, std::ios::binary);
        if (not ifs.is_open()) return false;

        const int32 w_rx = rx * 16;
        const int32 w_rz = rz * 16;

        for (int32 cx : range<int32>(w_rx, w_rx + 16)) {
            for (int32 cz : range<int32>(w_rz, w_rz + 16)) {
                bool chunk_exists = true;
				ifs.read(reinterpret_cast<char*>(&chunk_exists), sizeof(bool));

				if (not chunk_exists) continue;

				auto& chunk = get_or_create_chunk(cx, cz).value();
                std::unique_lock data_lock(chunk.data_mutex);

                chunk.clear();

                ifs.read(reinterpret_cast<char*>(&chunk.blocks[0][0][0]), sizeof(BlockStorage) * Chunk::SIZE_X * Chunk::SIZE_Y * Chunk::SIZE_Z);

                ifs.read(reinterpret_cast<char*>(&chunk.block_ids_size), sizeof(uint8));
                ifs.read(reinterpret_cast<char*>(&chunk.block_ids[0]), sizeof(uint32) * 256);

                uint8 id2block_size = 0;
                ifs.read(reinterpret_cast<char*>(&id2block_size), sizeof(uint8));
                for (auto j : range<uint8>(id2block_size)) {
                    uint32 global_id = 0;
                    ifs.read(reinterpret_cast<char*>(&global_id), sizeof(uint32));
                    ifs.read(reinterpret_cast<char*>(&chunk.id2block[global_id]), sizeof(uint8));
                }

                ifs.read(reinterpret_cast<char*>(&chunk.tag_ids_size), sizeof(uint8));
                ifs.read(reinterpret_cast<char*>(&chunk.tag_ids[0]), sizeof(std::pair<uint32, uint64>) * 256);

                uint8 id2tag_size = 0;
                ifs.read(reinterpret_cast<char*>(&id2tag_size), sizeof(uint8));
                for (auto j : range<uint8>(id2tag_size)) {
                    std::pair<uint32, uint64> global_id;
                    ifs.read(reinterpret_cast<char*>(&global_id.first), sizeof(uint32));
                    ifs.read(reinterpret_cast<char*>(&global_id.second), sizeof(uint64));
                    ifs.read(reinterpret_cast<char*>(&chunk.id2tag[global_id]), sizeof(uint8));
                }

                uint32 complex_size = 0;
                ifs.read(reinterpret_cast<char*>(&complex_size), sizeof(uint32));

                for (auto j : range<uint32>(complex_size)) {
                    Pos3D<uint8> key{};
                    BlockStorageFull value;

                    ifs.read(reinterpret_cast<char*>(&key.x), sizeof(uint8));
                    ifs.read(reinterpret_cast<char*>(&key.y), sizeof(uint8));
                    ifs.read(reinterpret_cast<char*>(&key.z), sizeof(uint8));

                    ifs.read(reinterpret_cast<char*>(&value.block_id), sizeof(uint32));
                    ifs.read(reinterpret_cast<char*>(&value.tag), sizeof(uint32));

                    chunk.complex_blocks.emplace(key, value);
                }

                chunk.chunk_version = 1;
                chunk.generated.store(true, std::memory_order_release);
                chunk.dirty.store(true, std::memory_order_release);
            }
        }

        return true;
    }

    void Server::_ready() {
        start_gc_thread();
        start_log_thread();

        TagRegistry::register_tag("face");
        TagRegistry::register_tag("transparent");
        TagRegistry::register_tag("collision_size");
        TagRegistry::register_tag("collision_offset");

        TagRegistry::set_value(TagRegistry::get_id("transparent"), 1, true);
        TagRegistry::set_value(TagRegistry::get_id("has_collision"), 1, true);
        TagRegistry::set_value(TagRegistry::get_id("collision_size"), 1, pack_vec3_mm(Vector3(0.2f, 0.6f, 0.2f)));
        TagRegistry::set_value(TagRegistry::get_id("collision_offset"), 1, pack_vec3_mm(Vector3(0.5f, 0.3f, 0.5f)));

        BlockRegistry::register_block<Air>          ("Air"           , "");
        BlockRegistry::register_block<Grass>        ("Grass Block"   , "");
        BlockRegistry::register_block<Dirt>         ("Dirt"          , "");
        BlockRegistry::register_block<Stone>        ("Stone"         , "");
        BlockRegistry::register_block<Pebble>       ("Pebble"        , "");
        BlockRegistry::register_block<OakLog>       ("Oak Log"       , "");
        BlockRegistry::register_block<OakPlanks>    ("Oak Planks"    , "");
        BlockRegistry::register_block<OakLeaves>    ("Oak Leaves"    , "");
        BlockRegistry::register_block<DiamondBlock> ("Diamond Block" , "");
        BlockRegistry::register_block<DiamondOre>   ("Diamond Ore"   , "");
        BlockRegistry::register_block<Bedrock>      ("Bedrock"       , "");
        BlockRegistry::register_block<RedstoneBlock>("Redstone Block", "");
        BlockRegistry::register_block<RedstoneDust> ("Redstone Dust" , "");

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

        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            log<LogType::ERROR>("WSAStartup failed");
            return;
        }

        server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (server_socket == INVALID_SOCKET) {
            log<LogType::ERROR>("Socket creation failed");
            WSACleanup();
            return;
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(8888);
        server_addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            log<LogType::ERROR>("Bind failed");
            closesocket(server_socket);
            WSACleanup();
            return;
        }

        if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
            log<LogType::ERROR>("Listen failed");
            closesocket(server_socket);
            WSACleanup();
            return;
        }

        u_long mode = 1;
        if (ioctlsocket(server_socket, FIONBIO, &mode) != 0) {
            log<LogType::ERROR>("Failed to set socket to non-blocking mode");
            closesocket(server_socket);
            WSACleanup();
            return;
        }

        log<LogType::INFO>("TCP Server listening on port 8888...");
    }

    void Server::_process(float64 delta) {
        {
            sockaddr_in client_addr{};
            int32 client_len = sizeof(client_addr);

            SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);
            if (client_socket != INVALID_SOCKET) {
                u_long mode = 1;
                ioctlsocket(client_socket, FIONBIO, &mode);

                clients[client_socket];
                log<LogType::INFO>(format{} << "Accepted client: " << client_socket);
            }
        }

        List<SOCKET> disconnected_clients;
        for (auto& [client_socket, client] : clients) {
            List<char> buffer;
            const auto recv_state = client.receive_queue.receive(client_socket, buffer);

            if (recv_state == ReceiveState::WAITING) continue;
            if (recv_state == ReceiveState::ERROR) {
                log<LogType::WARNING>(format{} << "Lost connect to client: " << client_socket);
                server_ptr.disconnect(client.name);
                disconnected_clients.append(client_socket);
                continue;
            }

            Message message = ReceiveQueue::parse(buffer);
            log<LogType::VERBOSE>(message.content);

            try {
                if (message.content == "Connect") {
                    Str const& name = message.arguments[0];
                    server_ptr.connect(name);
                    client.name = name;
					const auto player_pos = server_ptr.players[client.name].pos;
                    client.send_queue.store({ "Connected", { std::to_string(player_pos.x), std::to_string(player_pos.y), std::to_string(player_pos.z) } });
                }
                else if (message.content == "Chat") {
                    Str response = server_ptr.chat(message.arguments[0]);
                    client.send_queue.store({ "Chat response", { response.std_str() } });
                }
                else if (message.content == "Get chunk version") {
                    auto cx = std::stoi(message.arguments[0]);
                    auto cz = std::stoi(message.arguments[1]);

                    if (auto chunk_ptr = server_ptr.get_chunk(cx, cz)) client.send_queue.store({ "Chunk version", { std::to_string(chunk_ptr.value().chunk_version), message.arguments[0], message.arguments[1]}});
                    else client.send_queue.store({ "Chunk not ready", { message.arguments[0], message.arguments[1] } });
                }
                else if (message.content == "Get chunk data") {
                    auto cx = std::stoi(message.arguments[0]);
                    auto cz = std::stoi(message.arguments[1]);

                    auto chunk_ptr = server_ptr.get_chunk(cx, cz);
                    if (chunk_ptr and chunk_ptr.value().generated.load(std::memory_order_acquire)) {
                        const std::string world_data = server_ptr.serialize_chunk(cx, cz);
                        client.send_queue.store({ "Chunk data", { world_data, message.arguments[0], message.arguments[1] } });
                    }
                    else client.send_queue.store({ "Chunk not ready", { message.arguments[0], message.arguments[1] } });
                }
                else if (message.content == "Set block") {
                    server_ptr.set_global_block_id(std::stoi(message.arguments[0]), std::stoi(message.arguments[1]), std::stoi(message.arguments[2]), std::stoi(message.arguments[3]));
                    client.send_queue.store({ "Block set" });
                }
                else if (message.content == "Get players data") {
                    const std::string players_data = server_ptr.serialize_players();
                    client.send_queue.store({ "Players data", { players_data } });
                }
                else if (message.content == "Set seed and world name") {
                    int32 seed = std::stoi(message.arguments[0]);
                    Str world_name = message.arguments[1];
                    server_ptr.set_seed_and_world_name(seed, world_name);
                    client.send_queue.store({ "Set" });
                }
                else if (message.content == "Set render distance") {
                    int32 rd = std::stoi(message.arguments[0]);
                    server_ptr.set_render_distance(rd);
                    client.send_queue.store({ "Set" });
                }
                else if (message.content == "Set sleep time CPU") {
                    int32 stc = std::stoi(message.arguments[0]);
                    server_ptr.set_cpu_sleep_time(stc);
                    client.send_queue.store({ "Set" });
                }
                else if (message.content == "Update player pos") {
                    Pos3D<real> pos;
                    pos.x = (real)std::stod(message.arguments[1]);
                    pos.y = (real)std::stod(message.arguments[2]);
                    pos.z = (real)std::stod(message.arguments[3]);
                    server_ptr.update(message.arguments[0], pos);
                    client.send_queue.store({ "Updated" });
                }
                else client.send_queue.store({ "Invalid message content", { message.content.std_str() } });
            }
            catch (std::exception const& e) {
                log<LogType::ERROR>(format{} << "Error processing message: " << e.what());
                client.send_queue.store({ "Error", { e.what() } });
            }

            client.send_queue.send(client_socket);
        }

        for (auto const& client : disconnected_clients) {
            closesocket(client);
            clients.erase(client);
        }

        LogQueue::flush();
    }

    void Server::_exit_tree() {
        closesocket(server_socket);
        WSACleanup();

        running.store(false, std::memory_order_release);

        if (log_thread.joinable()) log_thread.join();
		if (gc_thread.joinable()) gc_thread.join();
    }

    void Server::start_gc_thread() {
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

    void Server::start_log_thread() {
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
}