module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
ENABLE_WARNING

module game.server;

using namespace std::chrono_literals;

namespace craftbuild {
    void Server::_ready() {
        start_gc_thread();
        start_log_thread();

        MetaRegistry::register_metadata("transparent");

        BlockRegistry::register_block<Air>("Air", "");
        BlockRegistry::register_block<Grass>("Grass Block", "");
        BlockRegistry::register_block<Dirt>("Dirt", "");
        BlockRegistry::register_block<Stone>("Stone", "");
        BlockRegistry::register_block<Pebble>("Pebble", "");
        BlockRegistry::register_block<OakLog>("Oak Log", "");
        BlockRegistry::register_block<OakPlanks>("Oak Planks", "");
        BlockRegistry::register_block<OakLeaves>("Oak Leaves", "");
        BlockRegistry::register_block<DiamondBlock>("Diamond Block", "");
        BlockRegistry::register_block<DiamondOre>("Diamond Ore", "");
        BlockRegistry::register_block<Bedrock>("Bedrock", "");
        BlockRegistry::register_block<RedstoneBlock>("Redstone Block", "");
        BlockRegistry::register_block<RedstoneDust>("Redstone Dust", "");

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

        tcp_server.instantiate();
        auto err = tcp_server->listen(8888);

        if (err != OK) {
            log<LogType::ERROR>("Failed to start TCP Server on port 8888");
            return;
        }

        log<LogType::INFO>("TCP Server listening on port 8888...");
    }

    void Server::_process(float64 delta) {
        if (tcp_server.is_valid() and tcp_server->is_connection_available()) {
            auto client_peer = tcp_server->take_connection();
            if (client_peer.is_valid()) {
                clients[client_peer].ip_addr = ""f << client_peer->get_connected_host().utf8() << ":" << client_peer->get_connected_port();
                log<LogType::INFO>("Accepted client: "f << clients[client_peer].ip_addr);
            }
        }

        List<Ref<StreamPeerTCP>> disconnected_clients;
        for (auto& [client_peer, client] : clients) {
            List<char> buffer;
            const auto recv_state = client.receive_queue.receive(**client_peer, buffer);

            if (recv_state == ReceiveState::WAITING) continue;
            if (recv_state == ReceiveState::ERROR) {
                log<LogType::WARNING>("Lost connect to client: "f << client.ip_addr);
                server.disconnect(client.name);
                disconnected_clients.append(client_peer);
                continue;
            }

            Message message = ReceiveQueue::parse(buffer);
            if (not message.content) continue;

            try {
                if (message.content == "Connect") {
                    Str name = message.arguments[0];

                    server.connect(name);
                    client.name = name;

                    const auto player_pos = server.players[client.name].pos;
                    client.send_queue.store({ "Connected", { std::to_string(player_pos.x), std::to_string(player_pos.y), std::to_string(player_pos.z), std::string(version) } });
                }
                else if (message.content == "Chat") {
                    Str response = server.chat(message.arguments[0]);
                    client.send_queue.store({ "Chat response", { response.std_str() } });
                }
                else if (message.content == "Get chunk version") {
                    auto cx = std::stoi(message.arguments[0]);
                    auto cy = std::stoi(message.arguments[1]);

                    if (auto chunk_ptr = server.get_chunk(cx, cy)) client.send_queue.store({ "Chunk version", { std::to_string(chunk_ptr.value().chunk_version), message.arguments[0], message.arguments[1]} });
                    else client.send_queue.store({ "Chunk not ready", { message.arguments[0], message.arguments[1] } });
                }
                else if (message.content == "Get chunk data") {
                    auto cx = std::stoi(message.arguments[0]);
                    auto cy = std::stoi(message.arguments[1]);

                    auto chunk_ptr = server.get_chunk(cx, cy);
                    if (chunk_ptr and chunk_ptr.value().generated.load(std::memory_order_acquire)) {
                        const std::string world_data = server.serialize_chunk(cx, cy);
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
                    server.set_cpu_sleep_time(stc);
                    client.send_queue.store({ "Set" });
                }
                else if (message.content == "Update player pos") {
                    Pos3D<real> pos{
                        (real)std::stod(message.arguments[1]),
                        (real)std::stod(message.arguments[2]),
                        (real)std::stod(message.arguments[3])
                    };
                    server.update(message.arguments[0], pos);
                    client.send_queue.store({ "Updated" });
                }
                else client.send_queue.store({ "Invalid message content", { message.content.std_str() } });
            }
            catch (std::exception const& e) {
                log<LogType::ERROR>("Error processing message: "f << message.content << " - " << e.what());
                client.send_queue.store({ "Error", { e.what() } });
            }

            client.send_queue.send(**client_peer);
        }

        for (auto const& client : disconnected_clients) {
            client->disconnect_from_host();
            clients.erase(client);
        }

        LogQueue::flush();
    }

    void Server::_exit_tree() {
        if (tcp_server.is_valid() and tcp_server->is_listening()) tcp_server->stop();

        for (auto& [client_peer, client] : clients) {
            if (not client_peer.is_valid()) continue;
            client_peer->disconnect_from_host();
        }

        clients.clear();
        running.store(false, std::memory_order_release);
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

        gc_thread = std::jthread(worker);
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

        log_thread = std::jthread(worker);
    }
}