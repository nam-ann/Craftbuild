module;

#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#undef ERROR

#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <includes.hpp>
#include <random>
#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <shared_mutex>

module game.server;

using byte = char;
import game.command;

namespace craftbuild {
    TCPServer::TCPServer() {
        start_log_thread();

        TagRegistry::register_tag("face");
        TagRegistry::register_tag("transparent");
        TagRegistry::set_value(TagRegistry::get_id("transparent"), 1, true);

        BlockRegistry::register_block<Air>         ("Air"          , "");
        BlockRegistry::register_block<Grass>       ("Grass Block"  , "grass_block.png");
        BlockRegistry::register_block<Dirt>        ("Dirt"         , "dirt.png");
        BlockRegistry::register_block<Stone>       ("Stone"        , "stone.png");
        BlockRegistry::register_block<Pebble>      ("Pebble"       , "pebble.png");
        BlockRegistry::register_block<OakLog>      ("Oak Log"      , "oak_log.png");
        BlockRegistry::register_block<OakPlanks>   ("Oak Planks"   , "oak_planks.png");
        BlockRegistry::register_block<OakLeaves>   ("Oak Leaves"   , "oak_leaves.png");
        BlockRegistry::register_block<DiamondBlock>("Diamond Block", "diamond_block.png");
        BlockRegistry::register_block<DiamondOre>  ("Diamond Ore"  , "diamond_ore.png");
        BlockRegistry::register_block<Bedrock>     ("Bedrock"      , "bedrock.png");

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

        command_ptr = new CommandInterpreter(this);

        noise.instantiate();
        noise->set_noise_type(FastNoiseLite::TYPE_SIMPLEX);
        noise->set_frequency(0.01f);

        if (not load_world(format{} << "user://game/saves/" << world_name << "/overworld.cbsave")) {
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

		if (log_thread.joinable()) log_thread.join();
		if (redstone_thread.joinable()) redstone_thread.join();
		if (scheduler_thread.joinable()) scheduler_thread.join();
        loop_cv.notify_all();

		save_world(format{} << "user://game/saves/" << world_name << "/overworld.cbsave");
    }

    none TCPServer::disconnect(const Str& player_name) {
        if (not players.contains(player_name)) return;

        std::unique_lock lock(player_mutex);
        online_players.erase(player_name);

        {
            std::lock_guard lock(current_player_mutex);
            current_player = online_players.begin();
        }

        log<LogType::INFO>(format{} << "Player disconnected: " << player_name);
    }

	none TCPServer::connect(const Str& player_name) {
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

	none TCPServer::update(const Str& player_name, const Pos3D<real>& new_pos) {
		std::unique_lock lock(player_mutex);
        players[player_name].pos = new_pos;
	}

    none TCPServer::start_log_thread() {
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

    none TCPServer::start_redstone_thread() {
        if (redstone_thread.joinable()) return;

        auto worker = [this]() {
            ThreadRegistry::register_thread("Redstone Thread");
            log<LogType::INFO>("Redstone thread started");

            while (running.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        };

        redstone_thread = std::thread(worker);
    }

    none TCPServer::start_scheduler_thread() {
        scheduler_thread = std::thread([this]() {
            ThreadRegistry::register_thread("Server Scheduler Thread");
            log<LogType::INFO>("Server scheduler thread started");

            current_player = online_players.begin();
            while (running.load(std::memory_order_relaxed)) {
                {
                    std::shared_lock lock1(player_mutex);
                    if (online_players.empty()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        continue;
                    }
                    
                    std::lock_guard lock2(current_player_mutex);
                    submit_jobs(players[current_player->first].pos);
                    if (++current_player == online_players.end()) current_player = online_players.begin();
                }

                std::unique_lock<std::mutex> lock(loop_mutex);
                loop_cv.wait_for(lock, std::chrono::milliseconds(sleep_time_cpu));
            }
        });
    }

    none TCPServer::submit_jobs(const Pos3D<real>& player) {
        int px = static_cast<int>(std::floor(player.x / Chunk::SIZE_X));
        int pz = static_cast<int>(std::floor(player.z / Chunk::SIZE_Z));
        for (auto r : range<int>(render_distance + 1)) {
            for (auto x : range<int>(-r, r + 1)) {
                for (auto z : range<int>(-r, r + 1)) {
                    if (std::abs(x) != r and std::abs(z) != r) continue;

                    Pos3D<int> chunk_pos{ px + x, 0, pz + z };
                    auto chunk_ptr = get_or_create_chunk(chunk_pos.x, chunk_pos.z);
                    auto& chunk = chunk_ptr.value();

                    if (chunk.generated.load(std::memory_order_acquire)) continue;

                    {
                        std::lock_guard lock(pending_jobs_mutex);
                        if (pending_terrain_jobs.contains(chunk_pos)) continue;
                        pending_terrain_jobs.insert(chunk_pos);
                    }

                    terrain_pool.enqueue([this, chunk_ptr, chunk_pos]() {
                        if (running.load(std::memory_order_relaxed)) {
                            auto& chunk = chunk_ptr.value();
                            chunk.generate_terrain(world_seed.load(), noise);

                            Pos3D<int> offsets[4] = { {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1} };
                            for (auto& o : offsets) {
                                auto n_ptr = get_chunk(chunk.chunk_pos.x + o.x, chunk.chunk_pos.z + o.z);
                                if (not n_ptr) continue;

                                auto& n = n_ptr.value();
                                if (n.generated.load(std::memory_order_acquire)) {
                                    n.dirty.store(true);
                                    ++n.chunk_version;
                                }
                            }
                        }

                        std::lock_guard lock(pending_jobs_mutex);
                        pending_terrain_jobs.erase(chunk_pos);
                    });
                }
            }
        }
    }

    std::string TCPServer::serialize_players() {
        std::stringstream os;

        {
            std::shared_lock lock(player_mutex);
            uint64 player_count = online_players.size();
            os.write(reinterpret_cast<const byte*>(&player_count), sizeof(uint64));

            for (const auto& [player_name, _] : online_players) {
                const auto& player_data = players[player_name];

                uint64 name_len = len(player_name);
                os.write(reinterpret_cast<const byte*>(&name_len), sizeof(uint64));
                os.write(reinterpret_cast<const byte*>(player_name.c_ptr()), name_len);
                os.write(reinterpret_cast<const byte*>(&player_data.hp), sizeof(uint8));
                os.write(reinterpret_cast<const byte*>(&player_data.pos), sizeof(Pos3D<real>));
                os.write(reinterpret_cast<const byte*>(&player_data.hotbar), sizeof(uint32) * PlayerData::HOTBAR_SIZE);
            }
        }

        return os.str();
    }

    std::string TCPServer::serialize_chunk(int cx, int cz) {
        std::stringstream os(std::ios::binary | std::ios::out);

		// Get & serialize chunk data
        Ptr<Chunk> chunk_ptr = get_chunk(cx, cz);
        if (not chunk_ptr) return "";
        auto& chunk = chunk_ptr.value();

        std::shared_lock data_lock(chunk.data_mutex);

        const auto* data = &chunk.blocks[0][0][0];
        os.write(reinterpret_cast<const byte*>(data), (uint64)Chunk::SIZE_X * Chunk::SIZE_Y * Chunk::SIZE_Z * sizeof(BlockStorage));
        
        os.write(reinterpret_cast<const byte*>(&chunk.block_ids_size), sizeof(uint8));
        os.write(reinterpret_cast<const byte*>(&chunk.block_ids[0]), sizeof(uint32) * 256);

        uint8 id2block_size = static_cast<uint8>(chunk.id2block.size());
        os.write(reinterpret_cast<const byte*>(&id2block_size), sizeof(uint8));
        for (const auto& [global_id, local_id] : chunk.id2block) {
            os.write(reinterpret_cast<const byte*>(&global_id), sizeof(uint32));
            os.write(reinterpret_cast<const byte*>(&local_id), sizeof(uint8));
        }

        os.write(reinterpret_cast<const byte*>(&chunk.tag_ids_size), sizeof(uint8));
        os.write(reinterpret_cast<const byte*>(&chunk.tag_ids[0]), sizeof(std::pair<uint32, uint64>) * 256);

        uint8 id2tag_size = static_cast<uint8>(chunk.id2tag.size());
        os.write(reinterpret_cast<const byte*>(&id2tag_size), sizeof(uint8));
        for (const auto& [global_id, local_id] : chunk.id2tag) {
            os.write(reinterpret_cast<const byte*>(&global_id.first), sizeof(uint32));
            os.write(reinterpret_cast<const byte*>(&global_id.second), sizeof(uint64));
            os.write(reinterpret_cast<const byte*>(&local_id), sizeof(uint8));
        }

        uint32 complex_size = static_cast<uint32>(chunk.complex_blocks.size());
        os.write(reinterpret_cast<const byte*>(&complex_size), sizeof(uint32));

        for (const auto& pair : chunk.complex_blocks) {
            os.write(reinterpret_cast<const byte*>(&pair.first.x), sizeof(uint8));
            os.write(reinterpret_cast<const byte*>(&pair.first.y), sizeof(uint8));
            os.write(reinterpret_cast<const byte*>(&pair.first.z), sizeof(uint8));

            os.write(reinterpret_cast<const byte*>(&pair.second.block_id), sizeof(uint32));
            os.write(reinterpret_cast<const byte*>(&pair.second.tag), sizeof(uint32));
        }

        os.write(reinterpret_cast<const byte*>(&chunk.chunk_version), sizeof(uint8));

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

    Ptr<Chunk> TCPServer::get_chunk(int cx, int cz) {
        std::shared_lock lock(chunks_mutex);
        Pos3D<int> cpos(cx, 0, cz);

        auto it = chunks.find(cpos);
        if (it == chunks.end()) return nullptr;
        return it->second;
    }

    Ptr<Chunk>& TCPServer::get_or_create_chunk(int cx, int cz) {
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
        chunks[chunk_pos] = std::move(chunk);
        return chunks[chunk_pos];
    }

    uint32 TCPServer::get_global_block_id(int wx, int wy, int wz) {
        if (wy < 0 or wy >= Chunk::SIZE_Y) return BlockRegistry::get_id("Air");

        int cx = static_cast<int>(std::floor((float32)wx / Chunk::SIZE_X));
        int cz = static_cast<int>(std::floor((float32)wz / Chunk::SIZE_Z));
        Pos3D<int> cpos(cx, 0, cz);

        Ptr<Chunk> chunk = get_chunk(cx, cz);
        if (not chunk) return BlockRegistry::get_id("Air");

        int lx = (wx % Chunk::SIZE_X + Chunk::SIZE_X) % Chunk::SIZE_X;
        int lz = (wz % Chunk::SIZE_Z + Chunk::SIZE_Z) % Chunk::SIZE_Z;

        return chunk.value().get_block({ (uint8)lx, (uint8)wy, (uint8)lz });
    }

    none TCPServer::set_global_block_id(uint32 block_id, int wx, int wy, int wz) {
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

    none TCPServer::set_seed_and_world_name(int32 seed, const Str& name) {}
    none TCPServer::set_render_distance(int32 rd) {}
    none TCPServer::set_sleep_time_cpu(int32 stc) {}

    Str TCPServer::chat(const Str& msg) {
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

    none TCPServer::save_world(const Str& path) {
        String real_path = ProjectSettings::get_singleton()->globalize_path(path.std_str().c_str());
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
        ofs.write(reinterpret_cast<const byte*>(&version_len), sizeof(uint64));

        ofs.write(version, sizeof(char) * version_len);

        std::vector<std::pair<Pos3D<int>, Ptr<Chunk>>> chunks_to_save;
        {
            std::shared_lock lock(chunks_mutex);

            uint32 seed = world_seed.load();
            ofs.write(reinterpret_cast<const byte*>(&seed), sizeof(uint32));

            for (const auto& E : chunks) {
                if (E.second.value().generated.load(std::memory_order_acquire)) {
                    chunks_to_save.emplace_back(E.first, E.second);
                }
            }
        }

        uint32 chunk_count = static_cast<uint32>(chunks_to_save.size());
        ofs.write(reinterpret_cast<const byte*>(&chunk_count), sizeof(uint32));

        for (const auto& [pos, chunk_ptr] : chunks_to_save) {
            auto& chunk = chunk_ptr.value();
            std::shared_lock data_lock(chunk.data_mutex);

            ofs.write(reinterpret_cast<const byte*>(&pos.x), sizeof(int32));
            ofs.write(reinterpret_cast<const byte*>(&pos.y), sizeof(int32));
            ofs.write(reinterpret_cast<const byte*>(&pos.z), sizeof(int32));

            const auto* data = &chunk.blocks[0][0][0];
            ofs.write(reinterpret_cast<const byte*>(data), Chunk::SIZE_X * Chunk::SIZE_Y * Chunk::SIZE_Z * sizeof(BlockStorage));
            
            ofs.write(reinterpret_cast<const byte*>(&chunk.block_ids_size), sizeof(uint8));
            ofs.write(reinterpret_cast<const byte*>(&chunk.block_ids[0]), sizeof(uint32) * 256);

            uint8 id2block_size = static_cast<uint8>(chunk.id2block.size());
            ofs.write(reinterpret_cast<const byte*>(&id2block_size), sizeof(uint8));
            for (const auto& [global_id, local_id] : chunk.id2block) {
                ofs.write(reinterpret_cast<const byte*>(&global_id), sizeof(uint32));
                ofs.write(reinterpret_cast<const byte*>(&local_id), sizeof(uint8));
            }

            ofs.write(reinterpret_cast<const byte*>(&chunk.tag_ids_size), sizeof(uint8));
            ofs.write(reinterpret_cast<const byte*>(&chunk.tag_ids[0]), sizeof(std::pair<uint32, uint64>) * 256);

            uint8 id2tag_size = static_cast<uint8>(chunk.id2tag.size());
            ofs.write(reinterpret_cast<const byte*>(&id2tag_size), sizeof(uint8));
            for (const auto& [global_id, local_id] : chunk.id2tag) {
                ofs.write(reinterpret_cast<const byte*>(&global_id.first), sizeof(uint32));
                ofs.write(reinterpret_cast<const byte*>(&global_id.second), sizeof(uint64));
                ofs.write(reinterpret_cast<const byte*>(&local_id), sizeof(uint8));
            }

            uint32 complex_size = static_cast<uint32>(chunk.complex_blocks.size());
            ofs.write(reinterpret_cast<const byte*>(&complex_size), sizeof(uint32));

            for (const auto& pair : chunk.complex_blocks) {
                ofs.write(reinterpret_cast<const byte*>(&pair.first.x), sizeof(uint8));
                ofs.write(reinterpret_cast<const byte*>(&pair.first.y), sizeof(uint8));
                ofs.write(reinterpret_cast<const byte*>(&pair.first.z), sizeof(uint8));

                ofs.write(reinterpret_cast<const byte*>(&pair.second.block_id), sizeof(uint32));
                ofs.write(reinterpret_cast<const byte*>(&pair.second.tag), sizeof(uint32));
            }
        }

        {
            std::shared_lock lock(player_mutex);
            const uint64 player_count = players.size();
            ofs.write(reinterpret_cast<const byte*>(&player_count), sizeof(uint64));

            for (const auto& [player_name, player_data] : players) {
				const uint64 player_name_len = len(player_name);
				ofs.write(reinterpret_cast<const byte*>(&player_name_len), sizeof(uint64));
                ofs.write(reinterpret_cast<const byte*>(player_name.c_ptr()), player_name_len);
                ofs.write(reinterpret_cast<const byte*>(&player_data.pos), sizeof(Pos3D<real>));
                ofs.write(reinterpret_cast<const byte*>(&player_data.hotbar), sizeof(uint32) * PlayerData::HOTBAR_SIZE);
            }
        }

        log<LogType::INFO>(format{} << "World saved!");
        log<LogType::VERBOSE>(format{} << chunk_count << " chunks");
    }

    bool TCPServer::load_world(const Str& path) {
        String real_path = ProjectSettings::get_singleton()->globalize_path(path.std_str().c_str());
        std::string std_path = (std::string)real_path.utf8();

        std::ifstream ifs(std_path, std::ios::binary);
        if (not ifs.is_open()) return false;

        log<LogType::INFO>("Loading world...");

        uint64 version_len = 0;
        ifs.read(reinterpret_cast<byte*>(&version_len), sizeof(uint64));

        char* current_version = new char[version_len + 1]; current_version[version_len] = '\0';
        ifs.read(current_version, sizeof(char) * version_len);
        if (strcmp(current_version, version)) log<LogType::WARNING>(format{} << "Save version" << "(" << current_version << ")" << " mismatch with current version (" << version << ")");
        delete[] current_version;

        uint32 seed = 0;
        ifs.read(reinterpret_cast<byte*>(&seed), sizeof(uint32));
        noise->set_seed(seed);
        world_seed.store(static_cast<int32>(seed), std::memory_order_release);

        uint32 chunk_count = 0;
        ifs.read(reinterpret_cast<byte*>(&chunk_count), sizeof(uint32));

        {
            std::unique_lock lock(chunks_mutex);
            chunks.clear();
        }

        for (auto i : range<uint32>(chunk_count)) {
            Pos3D<int> pos;
            ifs.read(reinterpret_cast<byte*>(&pos.x), sizeof(int32));
            ifs.read(reinterpret_cast<byte*>(&pos.y), sizeof(int32));
            ifs.read(reinterpret_cast<byte*>(&pos.z), sizeof(int32));

            auto& chunk = get_or_create_chunk(pos.x, pos.z).value();
            std::unique_lock data_lock(chunk.data_mutex);

            ifs.read(reinterpret_cast<byte*>(&chunk.blocks[0][0][0]), sizeof(BlockStorage) * Chunk::SIZE_X * Chunk::SIZE_Y * Chunk::SIZE_Z);

            ifs.read(reinterpret_cast<byte*>(&chunk.block_ids_size), sizeof(uint8));
            ifs.read(reinterpret_cast<byte*>(&chunk.block_ids[0]), sizeof(uint32) * 256);

            uint8 id2block_size = 0;
            ifs.read(reinterpret_cast<byte*>(&id2block_size), sizeof(uint8));
            for (auto j : range<uint8>(id2block_size)) {
                uint32 global_id = 0;
                uint8 local_id = 0;
                ifs.read(reinterpret_cast<byte*>(&global_id), sizeof(uint32));
                ifs.read(reinterpret_cast<byte*>(&local_id), sizeof(uint8));
                chunk.id2block[global_id] = local_id;
            }

            ifs.read(reinterpret_cast<byte*>(&chunk.tag_ids_size), sizeof(uint8));
            ifs.read(reinterpret_cast<byte*>(&chunk.tag_ids[0]), sizeof(std::pair<uint32, uint64>) * 256);

            uint8 id2tag_size = 0;
            ifs.read(reinterpret_cast<byte*>(&id2tag_size), sizeof(uint8));
            for (auto j : range<uint8>(id2tag_size)) {
                std::pair<uint32, uint64> global_id;
                uint8 local_id = 0;
                ifs.read(reinterpret_cast<byte*>(&global_id.first), sizeof(uint32));
                ifs.read(reinterpret_cast<byte*>(&global_id.second), sizeof(uint64));
                ifs.read(reinterpret_cast<byte*>(&local_id), sizeof(uint8));
                chunk.id2tag[global_id] = local_id;
            }

            uint32 complex_size = 0;
            ifs.read(reinterpret_cast<byte*>(&complex_size), sizeof(uint32));

            for (auto j : range<uint32>(complex_size)) {
                uint8 x, y, z;
                uint32 block_id, tag;

                ifs.read(reinterpret_cast<byte*>(&x), sizeof(uint8));
                ifs.read(reinterpret_cast<byte*>(&y), sizeof(uint8));
                ifs.read(reinterpret_cast<byte*>(&z), sizeof(uint8));

                ifs.read(reinterpret_cast<byte*>(&block_id), sizeof(uint32));
                ifs.read(reinterpret_cast<byte*>(&tag), sizeof(uint32));

                Pos3D<uint8> key{ x, y, z };
                BlockStorageFull value{ block_id, tag };

                chunk.complex_blocks.emplace(key, value);
            }

            chunk.chunk_version = 1;
            chunk.generated.store(true, std::memory_order_release);
            chunk.dirty.store(true, std::memory_order_release);
            chunk.collision_built.store(false, std::memory_order_release);
        }

        uint64 player_count = 0;
        ifs.read(reinterpret_cast<byte*>(&player_count), sizeof(uint64));

        {
            std::unique_lock lock(player_mutex);
            for (auto i : range<uint64>(player_count)) {
                Str player_name;
                uint64 player_name_len = 0;
                ifs.read(reinterpret_cast<byte*>(&player_name_len), sizeof(uint64));
                player_name.resize(player_name_len);
                ifs.read(reinterpret_cast<byte*>(player_name.c_ptr()), player_name_len);
                ifs.read(reinterpret_cast<byte*>(&players[player_name].pos), sizeof(Pos3D<real>));
                ifs.read(reinterpret_cast<byte*>(&players[player_name].hotbar), sizeof(uint32) * PlayerData::HOTBAR_SIZE);
            }
        }

        log<LogType::INFO>("World loaded successfully!");
        return true;
    }

    none Server::_ready() {
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

    none Server::_process(double delta) {
        {
            sockaddr_in client_addr{};
            int client_len = sizeof(client_addr);

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
                server.disconnect(client.name);
                disconnected_clients.append(client_socket);
                continue;
            }

            Message message = ReceiveQueue::parse(buffer);
            log<LogType::VERBOSE>(message.content);

            try {
                if (message.content == "Connect") {
                    const Str& name = message.arguments[0];
                    server.connect(name);
                    client.name = name;
                    client.send_queue.store({ "Connected" });
                }
                else if (message.content == "Chat") {
                    Str response = server.chat(message.arguments[0]);
                    client.send_queue.store({ "Chat response", { response.std_str() } });
                }
                else if (message.content == "Get chunk version") {
                    auto cx = std::stoi(message.arguments[0]);
                    auto cz = std::stoi(message.arguments[1]);

                    if (auto chunk_ptr = server.get_chunk(cx, cz)) client.send_queue.store({ "Chunk version", { std::to_string(chunk_ptr.value().chunk_version), message.arguments[0], message.arguments[1]}});
                    else client.send_queue.store({ "Chunk not ready", { message.arguments[0], message.arguments[1] } });
                }
                else if (message.content == "Get chunk data") {
                    auto cx = std::stoi(message.arguments[0]);
                    auto cz = std::stoi(message.arguments[1]);

                    auto chunk_ptr = server.get_chunk(cx, cz);
                    if (chunk_ptr and chunk_ptr.value().generated.load(std::memory_order_acquire)) {
                        const std::string world_data = server.serialize_chunk(cx, cz);
                        client.send_queue.store({ "Chunk data", { world_data, message.arguments[0], message.arguments[1] } });
                    }
                    else client.send_queue.store({ "Chunk not ready", { message.arguments[0], message.arguments[1] } });
                }
                else if (message.content == "Set block") {
                    server.set_global_block_id(std::stoi(message.arguments[0]), std::stoi(message.arguments[1]), std::stoi(message.arguments[2]), std::stoi(message.arguments[3]));
                    client.send_queue.store({ "Block set" });
                }
                else if (message.content == "Get players data") {
                    const std::string players_data = server.serialize_players();
                    client.send_queue.store({ "Players data", { players_data } });
                }
                else if (message.content == "Set seed and world name") {
                    int32 seed = std::stoi(message.arguments[0]);
                    Str world_name = message.arguments[1];
                    server.set_seed_and_world_name(seed, world_name);
                    client.send_queue.store({ "Set" });
                }
                else if (message.content == "Set render distance") {
                    int32 rd = std::stoi(message.arguments[0]);
                    server.set_render_distance(rd);
                    client.send_queue.store({ "Set" });
                }
                else if (message.content == "Set sleep time CPU") {
                    int32 stc = std::stoi(message.arguments[0]);
                    server.set_sleep_time_cpu(stc);
                    client.send_queue.store({ "Set" });
                }
                else if (message.content == "Update player pos") {
                    Pos3D<real> pos;
                    pos.x = std::stod(message.arguments[1]);
                    pos.y = std::stod(message.arguments[2]);
                    pos.z = std::stod(message.arguments[3]);
                    server.update(message.arguments[0], pos);
                    client.send_queue.store({ "Updated" });
                }
                else client.send_queue.store({ "Invalid message content", { message.content.std_str() } });
            }
            catch (const std::exception& e) {
                log<LogType::ERROR>(format{} << "Error processing message: " << e.what());
                client.send_queue.store({ "Error", { e.what() } });
            }

            client.send_queue.send(client_socket);
        }

        for (const auto& client : disconnected_clients) {
            closesocket(client);
            clients.erase(client);
        }

        LogQueue::flush();
    }

    none Server::_exit_tree() {
        closesocket(server_socket);
        WSACleanup();
    }
}