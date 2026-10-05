module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
ENABLE_WARNING

module game.world;

import game.command;

using namespace std::chrono_literals;

namespace craftbuild {
    void World::_get_refs(List<GCObject*>& refs) {
        std::shared_lock lock(chunks_mutex);
        for (auto const& [_, chunk_ptr] : chunks) {
            if (chunk_ptr) refs.append(chunk_ptr.object());
        }
    }

    World::World() {
        command_ptr = new CommandInterpreter(this);

        noise.instantiate();
        noise->set_noise_type(FastNoiseLite::TYPE_SIMPLEX);
        noise->set_frequency(0.01f);

        if (not load_world("user://game/saves/"f << world_name)) {
            log<LogType::WARNING>("Save file not found, starting new world.");
            if (world_seed.load(std::memory_order_acquire) == 0) {
                std::mt19937 generator;
                std::uniform_int_distribution<i32> distribution;
                world_seed.store(distribution(generator), std::memory_order_release);
            }
        }
        noise->set_seed(world_seed.load(std::memory_order_acquire));

        log<LogType::VERBOSE>("Assets loaded");

        start_redstone_thread();
        start_scheduler_thread();

        log<LogType::INFO>("Server initialized");
    }

    World::~World() {
        running.store(false, std::memory_order_relaxed);
        loop_cv.notify_all();
        save_world("user://game/saves/"f << world_name);
    }

    void World::disconnect(Str const& player_name) {
        if (not players.contains(player_name)) return;

        std::unique_lock lock(player_mutex);
        online_players.erase(player_name);

        {
            std::lock_guard lock(current_player_mutex);
            current_player = online_players.begin();
        }

        log<LogType::INFO>("Player disconnected: "f << player_name);
    }

    void World::connect(Str const& player_name) {
        std::unique_lock lock(player_mutex);
        online_players[player_name];

        {
            std::lock_guard lock(current_player_mutex);
            current_player = online_players.begin();
        }

        if (players.contains(player_name)) return;
        players[player_name] = PlayerData{ .name = player_name, .pos = { 0, 0, 0 } };

        log<LogType::INFO>("Player connected: "f << player_name);
    }

    void World::update(Str const& player_name, Pos3D<fsize> const& new_pos) {
        std::unique_lock lock(player_mutex);
        players[player_name].pos = new_pos;
    }

    void World::start_redstone_thread() {
        if (redstone_thread.joinable()) return;

        auto worker = [this]() {
            thread_registry::register_thread("Redstone");
            log<LogType::INFO>("Redstone thread started");

            while (running.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(10ms);
            }
            };

        redstone_thread = std::jthread(worker);
    }

    void World::start_scheduler_thread() {
        auto worker = [this]() {
            thread_registry::register_thread("Terrain");
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
                    Pos3D<fsize> pos;
                    {
                        std::shared_lock lock(player_mutex);
                        pos = players[current_player->first].pos;
                    }

                    submit_jobs(pos);

                    std::shared_lock lock2(player_mutex);
                    if (++current_player == online_players.end()) current_player = online_players.begin();
                }

                std::unique_lock lock(loop_mutex);
                loop_cv.wait_for(lock, std::chrono::milliseconds(cpu_sleep_time));
            }
        };

        scheduler_thread = std::jthread(worker);
    }

    void World::submit_jobs(Pos3D<fsize> const& player) {
        i32 px = static_cast<i32>(std::floor(player.x / Chunk::WIDTH));
        i32 pz = static_cast<i32>(std::floor(player.z / Chunk::WIDTH));

        auto get_or_load_or_create_chunk = [this](i32 cx, i32 cy) -> Ptr<Chunk> {
            if (auto chunk_ptr = get_or_load_chunk(cx, cy)) return chunk_ptr;
            return get_or_create_chunk(cx, cy);
        };

        auto process_cell = [&](i32 cx, i32 cy) {
            Pos2D<i32> chunk_pos{ px + cx, pz + cy };
            auto chunk_ptr = get_or_load_or_create_chunk(chunk_pos.x, chunk_pos.y);

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

                    Pos2D<i32> offsets[4] = { { 1,0 },{ -1,0 },{ 0,1 },{ 0,-1 } };
                    for (auto& o : offsets) {
                        auto n_ptr = get_or_load_chunk(chunk.chunk_pos.x + o.x, chunk.chunk_pos.y + o.y);
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

        for (auto r : range<i32>(1, render_distance + 1)) {
            for (auto x : range<i32>(-r, r + 1)) {
                process_cell(x, -r);
                process_cell(x, r);
            }
            for (auto z : range<i32>(-r + 1, r)) {
                process_cell(-r, z);
                process_cell(r, z);
            }
        }
    }

    std::string World::serialize_players() {
        std::stringstream os;

        {
            std::shared_lock lock(player_mutex);
            u64 player_count = online_players.size();
            os.write(reinterpret_cast<char const*>(&player_count), sizeof(u64));

            for (auto const& [player_name, _] : online_players) {
                auto const& player_data = players[player_name];

                u64 name_len = len(player_name);
                os.write(reinterpret_cast<char const*>(&name_len), sizeof(u64));
                os.write(reinterpret_cast<char const*>(player_name.data()), name_len);
                os.write(reinterpret_cast<char const*>(&player_data.hp), sizeof(u8));
                os.write(reinterpret_cast<char const*>(&player_data.pos), sizeof(Pos3D<fsize>));
                os.write(reinterpret_cast<char const*>(&player_data.hotbar), sizeof(u32) * PlayerData::HOTBAR_SIZE);
            }
        }

        return os.str();
    }

    std::string World::serialize_chunk(i32 cx, i32 cy) {
        std::stringstream os(std::ios::binary | std::ios::out);

        // Get & serialize chunk data
        Ptr<Chunk> chunk_ptr = get_chunk(cx, cy);
        if (not chunk_ptr) return "";
        auto& chunk = chunk_ptr.value();

        std::shared_lock data_lock(chunk.data_mutex);

        os.write(reinterpret_cast<char const*>(&chunk.blocks[0][0][0]), u64(Chunk::WIDTH * Chunk::HEIGHT * Chunk::WIDTH * sizeof(u8)));

        os.write(reinterpret_cast<char const*>(&chunk.block_ids_size), sizeof(u8));
        os.write(reinterpret_cast<char const*>(&chunk.block_ids[0]), sizeof(u32) * 256);

        u8 id2block_size = u8(chunk.id2block.size());
        os.write(reinterpret_cast<char const*>(&id2block_size), sizeof(u8));
        for (auto const& [global_id, local_id] : chunk.id2block) {
            os.write(reinterpret_cast<char const*>(&global_id), sizeof(u32));
            os.write(reinterpret_cast<char const*>(&local_id), sizeof(u8));
        }

        u64 meta_size = chunk.meta_ids.size();
        os.write(reinterpret_cast<char const*>(&meta_size), sizeof(u64));
        for (auto const& [pos, meta_storages] : chunk.meta_ids) {
            os.write(reinterpret_cast<char const*>(&pos.x), sizeof(u8));
            os.write(reinterpret_cast<char const*>(&pos.y), sizeof(u8));
            os.write(reinterpret_cast<char const*>(&pos.z), sizeof(u8));

            u64 meta_storages_size = meta_storages.size();
            os.write(reinterpret_cast<char const*>(&meta_storages_size), sizeof(u64));
            for (auto const& [key, value] : meta_storages) {
                os.write(reinterpret_cast<char const*>(&key), sizeof(u32));

                u64 data_size = len(value);
                os.write(reinterpret_cast<char const*>(&data_size), sizeof(u64));
                os.write(reinterpret_cast<char const*>(value.data()), data_size);
            }
        }

        u64 tag_size = chunk.tag_ids.size();
        os.write(reinterpret_cast<char const*>(&tag_size), sizeof(u64));
        for (auto const& [pos, tag_storages] : chunk.tag_ids) {
            os.write(reinterpret_cast<char const*>(&pos.x), sizeof(u8));
            os.write(reinterpret_cast<char const*>(&pos.y), sizeof(u8));
            os.write(reinterpret_cast<char const*>(&pos.z), sizeof(u8));

            u64 tag_storages_size = tag_storages.size();
            os.write(reinterpret_cast<char const*>(&tag_storages_size), sizeof(u64));
            for (auto key : tag_storages) {
                os.write(reinterpret_cast<char const*>(&key), sizeof(u32));
            }
        }

        u64 extended_block_size = chunk.extended_block_id.size();
        os.write(reinterpret_cast<char const*>(&extended_block_size), sizeof(u64));

        for (auto const& [pos, block_id] : chunk.extended_block_id) {
            os.write(reinterpret_cast<char const*>(&pos.x), sizeof(u8));
            os.write(reinterpret_cast<char const*>(&pos.y), sizeof(u8));
            os.write(reinterpret_cast<char const*>(&pos.z), sizeof(u8));

            os.write(reinterpret_cast<char const*>(&block_id), sizeof(u32));
        }

        os.write(reinterpret_cast<char const*>(&chunk.chunk_version), sizeof(u8));

        // Zip
        std::string raw_data = os.str();
        u32 uncompressed_size = u32(raw_data.size());

        PackedByteArray pba;
        pba.resize(uncompressed_size);
        memcpy(pba.ptrw(), raw_data.data(), uncompressed_size);

        PackedByteArray compressed_pba = pba.compress(FileAccess::COMPRESSION_ZSTD);

        PackedByteArray final_payload;
        final_payload.resize(sizeof(u32) + compressed_pba.size());

        memcpy(final_payload.ptrw(), &uncompressed_size, sizeof(u32));
        memcpy(final_payload.ptrw() + sizeof(u32), compressed_pba.ptr(), compressed_pba.size());

        return std::string(Marshalls::get_singleton()->raw_to_base64(final_payload).utf8());
    }

    Ptr<Chunk> World::get_chunk(i32 cx, i32 cy) {
        Pos2D<i32> cpos(cx, cy);

        std::shared_lock lock(chunks_mutex);
        auto it = chunks.find(cpos);

        if (it == chunks.end()) return nullptr;
        return it->second;
    }

    Ptr<Chunk> World::get_or_load_chunk(i32 cx, i32 cy) {
        if (auto chunk_ptr = get_chunk(cx, cy)) return chunk_ptr;

        const i32 rx = (cx >= 0) ? (cx / 16) : ((cx - 15) / 16);
        const i32 ry = (cy >= 0) ? (cy / 16) : ((cy - 15) / 16);

        const Str path = "user://game/saves/"f << world_name;
        const String real_path = ProjectSettings::get_singleton()->globalize_path((path + "/regions/" + Str(rx) + "_" + Str(ry) + ".cbregion").std_str().c_str());
        const std::string std_path = std::string(real_path.utf8());

        const auto chunk_pos = Pos2D<i32>{ cx, cy };

        if (std::filesystem::exists(std_path)) {
            load_region(path, rx, ry);

            std::shared_lock lock(chunks_mutex);
            auto it_loaded = chunks.find(chunk_pos);
            if (it_loaded != chunks.end()) return it_loaded->second;
        }

        return nullptr;
    };

    Ptr<Chunk> World::get_or_create_chunk(i32 cx, i32 cy) {
        Pos2D<i32> chunk_pos{ cx, cy };
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

    u32 World::get_global_block_id(i32 wx, i32 wy, i32 wz) {
        if (wy < 0 or wy >= Chunk::HEIGHT) return block_registry::get_id("Air");

        i32 cx = i32(std::floor(f32(wx) / Chunk::WIDTH));
        i32 cy = i32(std::floor(f32(wz) / Chunk::WIDTH));
        Pos2D<i32> cpos(cx, cy);

        Ptr<Chunk> chunk = get_chunk(cx, cy);
        if (not chunk) return block_registry::get_id("Air");

        i32 lx = (wx % Chunk::WIDTH + Chunk::WIDTH) % Chunk::WIDTH;
        i32 lz = (wz % Chunk::WIDTH + Chunk::WIDTH) % Chunk::WIDTH;

        return chunk.value().get_block({ u8(lx), u8(wy), u8(lz) });
    }

    void World::set_global_block_id(u32 block_id, i32 wx, i32 wy, i32 wz) {
        if (wy < 0 or wy >= Chunk::HEIGHT) return;

        i32 cx = i32(std::floor(f32(wx) / Chunk::WIDTH));
        i32 cy = i32(std::floor(f32(wz) / Chunk::WIDTH));
        Pos2D<i32> cpos(cx, cy);

        Ptr<Chunk> chunk = get_chunk(cx, cy);
        if (not chunk) return;

        i32 lx = (wx % Chunk::WIDTH + Chunk::WIDTH) % Chunk::WIDTH;
        i32 lz = (wz % Chunk::WIDTH + Chunk::WIDTH) % Chunk::WIDTH;

        chunk.value().set_block({ u8(lx), u8(wy), u8(lz) }, block_id);
    }

    void World::unload_distant_chunks() {
        const i32 unload_dist = render_distance + 4;
        Set<Pos2D<i32>> still_viewing_chunks;

        {
            std::shared_lock lock(chunks_mutex);
            for (auto const& [chunk_pos, _] : chunks) {
                std::shared_lock lock(player_mutex);
                for (auto const& [player_name, _] : online_players) {
                    auto& player_pos = players[player_name].pos;
                    i32 dx = std::abs(chunk_pos.x - i32(std::floor(player_pos.x / Chunk::WIDTH)));
                    i32 dz = std::abs(chunk_pos.y - i32(std::floor(player_pos.z / Chunk::WIDTH)));
                    if (dx <= unload_dist and dz <= unload_dist) still_viewing_chunks.insert(chunk_pos);
                }
            }
        }

        Set<Pos2D<i32>> regions_to_save;

        {
            std::unique_lock lock(chunks_mutex);
            for (auto it = chunks.begin(); it != chunks.end();) {
                auto const& chunk_pos = it->first;

                if (still_viewing_chunks.find(chunk_pos) == still_viewing_chunks.end()) {
                    i32 rx = (chunk_pos.x >= 0) ? (chunk_pos.x / 16) : ((chunk_pos.x - 15) / 16);
                    i32 ry = (chunk_pos.y >= 0) ? (chunk_pos.y / 16) : ((chunk_pos.y - 15) / 16);

                    regions_to_save.insert({ rx, ry });
                    it = chunks.erase(it);
                }
                else ++it;
            }
        }

        for (auto const& [rx, ry] : regions_to_save) {
            save_region("user://game/saves/"f << world_name, rx, ry);
        }
    }

    void World::set_seed_and_world_name(i32 seed, Str const& name) {
        world_seed.store(seed, std::memory_order_release);
        noise->set_seed(seed);
        world_name = name;
    }

    void World::set_render_distance(i32 rd) { render_distance = rd; }
    void World::set_cpu_sleep_time(i32 stc) { cpu_sleep_time = stc; }

    Str World::chat(Str const& msg) {
        if (msg) {
            std::string _msg = msg.std_str();
            log<LogType::NORMAL>("[Player] "f << _msg);
            if (_msg.starts_with("/")) {
                CommandInterpreter* interpreter = static_cast<CommandInterpreter*>(command_ptr);
                return interpreter->execute_command(_msg.erase(0, 1)).std_str().c_str();
            }
            else return msg;
        }
        return "";
    }

    void World::save_world(Str const& path) {
        String real_path = ProjectSettings::get_singleton()->globalize_path((path + "/" + world_name + ".cbworld").std_str().c_str());
        std::string std_path = std::string(real_path.utf8());

        // Tạo thư mục
        std::filesystem::create_directories(std::filesystem::path(std_path).parent_path());

        std::ofstream ofs(std_path, std::ios::binary);
        if (not ofs.is_open()) {
            log<LogType::ERROR>("Cannot open save file: "f << std_path);
            return;
        }

        log<LogType::INFO>("Saving world...");

        u64 version_len = version.size();
        ofs.write(reinterpret_cast<char const*>(&version_len), sizeof(u64));
        ofs.write(version.data(), sizeof(char) * version_len);

        u32 seed = world_seed.load(std::memory_order_release);
        ofs.write(reinterpret_cast<char const*>(&seed), sizeof(u32));

        {
            std::shared_lock lock(player_mutex);
            const u64 player_count = players.size();
            ofs.write(reinterpret_cast<char const*>(&player_count), sizeof(u64));

            for (auto const& [player_name, player_data] : players) {
                const u64 player_name_len = len(player_name);
                ofs.write(reinterpret_cast<char const*>(&player_name_len), sizeof(u64));
                ofs.write(reinterpret_cast<char const*>(player_name.data()), player_name_len);
                ofs.write(reinterpret_cast<char const*>(&player_data.hp), sizeof(u8));
                ofs.write(reinterpret_cast<char const*>(&player_data.pos), sizeof(Pos3D<fsize>));
                ofs.write(reinterpret_cast<char const*>(&player_data.hotbar), sizeof(u32) * PlayerData::HOTBAR_SIZE);
            }
        }

        ofs.close();

        Set<Pos2D<i32>> regions_to_save;

        {
            std::shared_lock lock(chunks_mutex);
            for (auto const& [chunk_pos, _] : chunks) {
                i32 rx = (chunk_pos.x >= 0) ? (chunk_pos.x / 16) : ((chunk_pos.x - 15) / 16);
                i32 ry = (chunk_pos.y >= 0) ? (chunk_pos.y / 16) : ((chunk_pos.y - 15) / 16);
                regions_to_save.insert({ rx, ry });
            }
        }

        for (auto const& [rx, ry] : regions_to_save) save_region(path, rx, ry);

        log<LogType::INFO>("World saved!");
    }

    bool World::load_world(Str const& path) {
        String real_path = ProjectSettings::get_singleton()->globalize_path(Str(""f << path << "/" << world_name << ".cbworld").std_str().c_str());
        std::string std_path = std::string(real_path.utf8());

        std::ifstream ifs(std_path, std::ios::binary);
        if (not ifs.is_open()) return false;

        log<LogType::INFO>("Loading world...");

        u64 version_len = 0;
        ifs.read(reinterpret_cast<char*>(&version_len), sizeof(u64));

        std::string current_version(version_len, '\0');
        ifs.read(current_version.data(), sizeof(char) * version_len);
        if (current_version != version) {
            log<LogType::WARNING>("Save version"f << "(" << current_version << ")" << " mismatch with current version(" << version << ")");
            log<LogType::WARNING>("This game's world loader doesn't support Data Migration. World data might get damaged and cause crashes");
        }

        u32 seed = 0;
        ifs.read(reinterpret_cast<char*>(&seed), sizeof(u32));
        noise->set_seed(seed);
        world_seed.store(static_cast<i32>(seed), std::memory_order_release);

        u64 player_count = 0;
        ifs.read(reinterpret_cast<char*>(&player_count), sizeof(u64));

        {
            std::unique_lock lock(player_mutex);
            for (auto i : range<u64>(player_count)) {
                Str player_name;
                u64 player_name_len = 0;
                ifs.read(reinterpret_cast<char*>(&player_name_len), sizeof(u64));
                player_name.resize(player_name_len);
                ifs.read(reinterpret_cast<char*>(player_name.data()), player_name_len);
                ifs.read(reinterpret_cast<char*>(&players[player_name].hp), sizeof(u8));
                ifs.read(reinterpret_cast<char*>(&players[player_name].pos), sizeof(Pos3D<fsize>));
                ifs.read(reinterpret_cast<char*>(&players[player_name].hotbar), sizeof(u32) * PlayerData::HOTBAR_SIZE);
            }
        }

        log<LogType::INFO>("World loaded successfully!");
        return true;
    }

    void World::save_region(Str const& path, i32 rx, i32 ry) {
        String real_path = ProjectSettings::get_singleton()->globalize_path(Str(""f << path << "/regions/" << rx << "_" << ry << ".cbregion").std_str().c_str());
        std::string std_path = std::string(real_path.utf8());

        std::filesystem::create_directories(std::filesystem::path(std_path).parent_path());

        std::ofstream ofs(std_path, std::ios::binary);
        if (not ofs.is_open()) {
            log<LogType::ERROR>("Cannot open save file: "f << std_path);
            return;
        }

        const i32 cx = rx * 16;
        const i32 cy = ry * 16;

        for (i32 x : range<i32>(cx, cx + 16)) {
            for (i32 z : range<i32>(cy, cy + 16)) {
                auto chunk_ptr = get_chunk(x, z);

                bool const chunk_exists = chunk_ptr and chunk_ptr.value().generated.load(std::memory_order_acquire);
                ofs.write(reinterpret_cast<char const*>(&chunk_exists), sizeof(bool));

                if (not chunk_exists) continue;

                auto& chunk = chunk_ptr.value();
                std::shared_lock data_lock(chunk.data_mutex);

                ofs.write(reinterpret_cast<char const*>(&chunk.blocks[0][0][0]), Chunk::WIDTH * Chunk::HEIGHT * Chunk::WIDTH * sizeof(u8));

                ofs.write(reinterpret_cast<char const*>(&chunk.block_ids_size), sizeof(u8));
                ofs.write(reinterpret_cast<char const*>(&chunk.block_ids[0]), sizeof(u32) * 256);

                u8 id2block_size = u8(chunk.id2block.size());
                ofs.write(reinterpret_cast<char const*>(&id2block_size), sizeof(u8));
                for (auto const& [global_id, local_id] : chunk.id2block) {
                    ofs.write(reinterpret_cast<char const*>(&global_id), sizeof(u32));
                    ofs.write(reinterpret_cast<char const*>(&local_id), sizeof(u8));
                }

                u64 meta_size = chunk.meta_ids.size();
                ofs.write(reinterpret_cast<char const*>(&meta_size), sizeof(u64));
                for (auto const& [pos, meta_storages] : chunk.meta_ids) {
                    ofs.write(reinterpret_cast<char const*>(&pos.x), sizeof(u8));
                    ofs.write(reinterpret_cast<char const*>(&pos.y), sizeof(u8));
                    ofs.write(reinterpret_cast<char const*>(&pos.z), sizeof(u8));

                    u64 meta_storages_size = meta_storages.size();
                    ofs.write(reinterpret_cast<char const*>(&meta_storages_size), sizeof(u64));
                    for (auto const& [key, value] : meta_storages) {
                        ofs.write(reinterpret_cast<char const*>(&key), sizeof(u32));

                        u64 data_size = len(value);
                        ofs.write(reinterpret_cast<char const*>(&data_size), sizeof(u64));
                        ofs.write(reinterpret_cast<char const*>(value.data()), data_size);
                    }
                }

                u64 tag_size = chunk.tag_ids.size();
                ofs.write(reinterpret_cast<char const*>(&tag_size), sizeof(u64));
                for (auto const& [pos, tag_storages] : chunk.tag_ids) {
                    ofs.write(reinterpret_cast<char const*>(&pos.x), sizeof(u8));
                    ofs.write(reinterpret_cast<char const*>(&pos.y), sizeof(u8));
                    ofs.write(reinterpret_cast<char const*>(&pos.z), sizeof(u8));

                    u64 tag_storages_size = tag_storages.size();
                    ofs.write(reinterpret_cast<char const*>(&tag_storages_size), sizeof(u64));
                    for (auto key : tag_storages) {
                        ofs.write(reinterpret_cast<char const*>(&key), sizeof(u32));
                    }
                }

                u64 extended_block_size = chunk.extended_block_id.size();
                ofs.write(reinterpret_cast<char const*>(&extended_block_size), sizeof(u64));

                for (auto const& [pos, block_id] : chunk.extended_block_id) {
                    ofs.write(reinterpret_cast<char const*>(&pos.x), sizeof(u8));
                    ofs.write(reinterpret_cast<char const*>(&pos.y), sizeof(u8));
                    ofs.write(reinterpret_cast<char const*>(&pos.z), sizeof(u8));

                    ofs.write(reinterpret_cast<char const*>(&block_id), sizeof(u32));
                }
            }
        }
    }

    bool World::load_region(Str const& path, i32 rx, i32 ry) {
        String real_path = ProjectSettings::get_singleton()->globalize_path(Str(""f << path << "/regions/" << rx << "_" << ry << ".cbregion").std_str().c_str());
        std::string std_path = std::string(real_path.utf8());

        std::ifstream ifs(std_path, std::ios::binary);
        if (not ifs.is_open()) return false;

        i32 const w_rx = rx * 16;
        i32 const w_rz = ry * 16;

        for (i32 cx : range<i32>(w_rx, w_rx + 16)) {
            for (i32 cy : range<i32>(w_rz, w_rz + 16)) {
                bool chunk_exists = true;
                ifs.read(reinterpret_cast<char*>(&chunk_exists), sizeof(bool));

                if (not chunk_exists) continue;

                auto& chunk = get_or_create_chunk(cx, cy).value();
                std::unique_lock data_lock(chunk.data_mutex);

                chunk.clear();

                ifs.read(reinterpret_cast<char*>(&chunk.blocks[0][0][0]), sizeof(u8) * Chunk::WIDTH * Chunk::HEIGHT * Chunk::WIDTH);

                ifs.read(reinterpret_cast<char*>(&chunk.block_ids_size), sizeof(u8));
                ifs.read(reinterpret_cast<char*>(&chunk.block_ids[0]), sizeof(u32) * 256);

                u8 id2block_size = 0;
                ifs.read(reinterpret_cast<char*>(&id2block_size), sizeof(u8));
                for (auto j : range(id2block_size)) {
                    u32 global_id = 0;
                    ifs.read(reinterpret_cast<char*>(&global_id), sizeof(u32));
                    ifs.read(reinterpret_cast<char*>(&chunk.id2block[global_id]), sizeof(u8));
                }

                u64 meta_size = 0;
                ifs.read(reinterpret_cast<char*>(&meta_size), sizeof(u64));
                for (auto j : range(meta_size)) {
                    Pos3D<u8> pos = {};

                    ifs.read(reinterpret_cast<char*>(&pos.x), sizeof(u8));
                    ifs.read(reinterpret_cast<char*>(&pos.y), sizeof(u8));
                    ifs.read(reinterpret_cast<char*>(&pos.z), sizeof(u8));

                    auto& meta_storages = chunk.meta_ids[pos];

                    u64 meta_storages_size = 0;
                    ifs.read(reinterpret_cast<char*>(&meta_storages_size), sizeof(u64));
                    for (auto k : range(meta_storages_size)) {
                        u32 key = 0;
                        ifs.read(reinterpret_cast<char*>(&key), sizeof(u32));

                        auto& value = meta_storages[key];

                        u64 data_size = len(value);
                        ifs.read(reinterpret_cast<char*>(&data_size), sizeof(u64));
                        ifs.read(reinterpret_cast<char*>(value.data()), data_size);
                    }
                }

                u64 tag_size = 0;
                ifs.read(reinterpret_cast<char*>(&tag_size), sizeof(u64));
                for (auto j : range(tag_size)) {
                    Pos3D<u8> pos = {};

                    ifs.read(reinterpret_cast<char*>(&pos.x), sizeof(u8));
                    ifs.read(reinterpret_cast<char*>(&pos.y), sizeof(u8));
                    ifs.read(reinterpret_cast<char*>(&pos.z), sizeof(u8));

                    auto& tag_storages = chunk.tag_ids[pos];

                    u64 tag_storages_size = 0;
                    ifs.read(reinterpret_cast<char*>(&tag_storages_size), sizeof(u64));
                    for (auto k : range(tag_storages_size)) {
                        u32 key = 0;
                        ifs.read(reinterpret_cast<char*>(&key), sizeof(u32));

                        tag_storages.insert(key);
                    }
                }

                u64 extended_block_size = 0;
                ifs.read(reinterpret_cast<char*>(&extended_block_size), sizeof(u64));

                for (auto j : range(extended_block_size)) {
                    Pos3D<u8> pos = {};

                    ifs.read(reinterpret_cast<char*>(&pos.x), sizeof(u8));
                    ifs.read(reinterpret_cast<char*>(&pos.y), sizeof(u8));
                    ifs.read(reinterpret_cast<char*>(&pos.z), sizeof(u8));

                    ifs.read(reinterpret_cast<char*>(&chunk.extended_block_id[pos]), sizeof(u32));
                }

                chunk.chunk_version = 1;
                chunk.generated.store(true, std::memory_order_release);
                chunk.dirty.store(true, std::memory_order_release);
            }
        }

        return true;
    }
}