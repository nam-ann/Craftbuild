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

    class Server : public Node {
        GDCLASS(Server, Node)

        WSADATA wsa_data;
        SOCKET server_socket;
        sockaddr_in server_addr{};
        TCPServer server;
        ReceiveQueue receive_queue;
		Dict<SOCKET, SendQueue> clients;
        Dict<SOCKET, Str> player_names;

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
            ioctlsocket(server_socket, FIONBIO, &mode);

            log<LogType::INFO>("TCP Server listening on port 8888...");
        }

        none _process(double delta) override {
            List<char> buffer;

            {
                sockaddr_in client_addr{};
                int client_len = sizeof(client_addr);

                SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);
                if (client_socket != INVALID_SOCKET) clients[client_socket] = SendQueue{};
            }

			std::vector<SOCKET> disconnected_clients;
            for (auto& [client_socket, client] : clients) {
                const auto recv_state = receive_queue.receive(client_socket, buffer);

                if (recv_state == ReceiveState::WAITING) continue;
                if (recv_state == ReceiveState::ERROR) {
                    server.disconnect(player_names[client_socket]);
                    disconnected_clients.push_back(client_socket);
                    continue;
                }

                Message message = ReceiveQueue::parse(buffer);

                try {
                    if (message.content == "Connect") {
                        const Str& name = message.arguments[0];
                        server.connect(name);
                        player_names[client_socket] = name;
                        client.store({ "Connected" });
                    }
                    else if (message.content == "Chat") {
                        Str response = server.chat(message.arguments[0]);
                        client.store({ "Chat response", { response.std_str() }});
                    }
                    else if (message.content == "Get chunk data") {
                        const std::string world_data = server.serialize_chunk(std::stoi(message.arguments[0]), std::stoi(message.arguments[1]));
                        client.store({ "Chunk data", { world_data } });
                    }
                    else if (message.content == "Set seed and world name") {
                        int32 seed = std::stoi(message.arguments[0]);
                        Str world_name = message.arguments[1];
                        server.set_seed_and_world_name(seed, world_name);
                        client.store({ "Set" });
                    }
                    else if (message.content == "Set render distance") {
                        int32 rd = std::stoi(message.arguments[0]);
                        server.set_render_distance(rd);
                        client.store({ "Set" });
                    }
                    else if (message.content == "Set sleep time CPU") {
                        int32 stc = std::stoi(message.arguments[0]);
                        server.set_sleep_time_cpu(stc);
                        client.store({ "Set" });
                    }
                    else if (message.content == "Update player pos") {
                        Pos3D<real> pos;
                        pos.x = std::stod(message.arguments[1]);
                        pos.y = std::stod(message.arguments[2]);
                        pos.z = std::stod(message.arguments[3]);
                        server.update(message.arguments[0], pos);
                        client.store({ "Updated" });
                    }
                }
                catch (const std::exception& e) {
                    log<LogType::ERROR>(format{} << "Error processing message: " << e.what());
                    client.store({ "Error" });
                }

                client.send(client_socket);
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