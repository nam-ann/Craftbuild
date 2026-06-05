module;

#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#undef ERROR

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>

#include <includes.hpp>
#include <string>
#include <sstream>
#include <shared_mutex>
#include <unordered_set>
#include <unordered_map>

export module game.server;

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
import game.network;
import game.world.cave;
import game.world.chunk;
import game.world.biome;
import game.block.normal_blocks;

using namespace godot;

export namespace craftbuild {
	class TCPServer {
        Dict<Pos3D<int32>, Ptr<Chunk>> chunks;
        mutable std::shared_mutex chunks_mutex;

        Ref<FastNoiseLite> noise;
        std::atomic<int32> world_seed = 0;
        Str world_name = "My World";

		std::shared_mutex player_mutex;
        Dict<Str, Pos3D<real>> players;
		std::vector<Str> player_names;
		size current_player = 0;
        none* command_ptr = nullptr;

        std::atomic<bool> running = true;
        std::thread log_thread;
        std::thread redstone_thread;
        std::thread scheduler_thread;
        ThreadPool terrain_pool{ 4 };
        std::unordered_set<Pos3D<int>, Hasher<Pos3D<int>>> pending_terrain_jobs;
        std::mutex pending_jobs_mutex;

        std::atomic<bool> pausing = true;
        std::atomic<bool> chatting = false;
        std::mutex loop_mutex;
        std::condition_variable loop_cv;

    public:
        inline static int32 SIZE_X = render_distance * 16;
        inline static int32 SIZE_Z = render_distance * 16;

        TCPServer(bool multiplayer = true);
        ~TCPServer();
        none connect(const Str& player_name);
        none disconnect(const Str& player_name);
        none update(const Str& player_name, const Pos3D<real>& new_pos);

        none start_log_thread();
        none start_redstone_thread();
        none start_scheduler_thread();
        none submit_jobs(const Pos3D<real>& player);

        std::string serialize_players();
        std::string serialize_chunk(int cx, int cz);
        Ptr<Chunk> get_chunk(int cx, int cz);
        Ptr<Chunk> get_or_create_chunk(int cx, int cz);
        uint32 get_global_block_id(int wx, int wy, int wz);
        none set_global_block_id(uint32 block_id, int wx, int wy, int wz);

        none set_seed_and_world_name(int32 seed, const Str& name);
        none set_render_distance(int32 rd);
        none set_sleep_time_cpu(int32 stc);

        Str chat(const Str& message);

        none save_world(const Str& path);
        bool load_world(const Str& path);

        friend class CommandInterpreter;
        friend none craftbuild_mod_main();
	};

    struct Client {
        ReceiveQueue receive_queue;
        SendQueue send_queue;
        Str name;
    };

    class Server : public Node {
        GDCLASS(Server, Node)

        WSADATA wsa_data;
        SOCKET server_socket;
        sockaddr_in server_addr{};
        TCPServer server;
		Dict<SOCKET, Client> clients;

    public:
        none _ready() override {
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

        none _process(double delta) override {
            {
                sockaddr_in client_addr{};
                int client_len = sizeof(client_addr);

                SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);
                if (client_socket != INVALID_SOCKET) {
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

                try {
                    if (message.content == "Connect") {
                        const Str& name = message.arguments[0];
                        server.connect(name);
                        client.name = name;
                        client.send_queue.store({ "Connected" });
                    }
                    else if (message.content == "Chat") {
                        Str response = server.chat(message.arguments[0]);
                        client.send_queue.store({ "Chat response", { response.std_str() }});
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
                }
                catch (const std::exception& e) {
                    log<LogType::ERROR>(format{} << "Error processing message: " << e.what());
                    client.send_queue.store({ "Error", { e.what() }});
                }

                client.send_queue.send(client_socket);
            }

			for (const auto& client : disconnected_clients) {
				closesocket(client);
                clients.erase(client);
			}

            LogQueue::flush();
        }

        none _exit_tree() override {
            closesocket(server_socket);
            WSACleanup();
        }

		static none _bind_methods() {}
    };
}