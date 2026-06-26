module;

#include <includes.hpp>

module game.command;

import game.server;
import game.world.chunk;

namespace craftbuild {
	bool CommandInterpreter::is_valid_coordinate(int64 x, int64 y, int64 z) {
        return (x >= -TCPServer::SIZE_X and x <= TCPServer::SIZE_X and y >= 0 and y <= Chunk::SIZE_Y and z >= -TCPServer::SIZE_Z and z <= TCPServer::SIZE_Z);
    }

    Str CommandInterpreter::execute_set_block(const std::vector<Str>& args) {
        TCPServer* world = static_cast<TCPServer*>(world_ptr);
        if (not world) return "";
        Str output;

        if (args.size() < 5) {
            output = "Must fill atleast (x y z block_type)";
            log<LogType::ERROR>(output);
            return output;
        }

        try {
            int64 x = std::stoi(args[1].std_str());
            int64 y = std::stoi(args[2].std_str());
            int64 z = std::stoi(args[3].std_str());
            Str block_type = args[4];

            if (not is_valid_coordinate(x, y, z)) {
                output = format{} << "(" << x << "," << y << "," << z << ") outs of bounds";
                log<LogType::ERROR>(output);
                return output;
            }

            if (not is_valid_block_type(block_type)) {
                output = format{} << "Invalid block: '" << block_type;
                log<LogType::ERROR>(output);
                return output;
            }

            world->set_global_block_id(BlockRegistry::get_id(block_type), x, y, z);
            output = format{} << "Set block " << block_type << " at (" << x << "," << y << "," << z << ")";
            log<LogType::INFO>(output);
        }
        catch (const std::exception& e) {
            output = "Invalid command arguments";
            log<LogType::ERROR>(output);
        }
        return output;
    }

    Str CommandInterpreter::execute_fill(const std::vector<Str>& args) {
        TCPServer* world = static_cast<TCPServer*>(world_ptr);
        if (not world) return "";
        Str output;

        if (args.size() < 8) {
            output = "Must fill atleast (x1 y1 z1 x2 y2 z2 block_type)";
            log<LogType::ERROR>(output);
            return output;
        }

        try {
            int64 x1 = std::stoi(args[1].std_str());
            int64 y1 = std::stoi(args[2].std_str());
            int64 z1 = std::stoi(args[3].std_str());
            int64 x2 = std::stoi(args[4].std_str());
            int64 y2 = std::stoi(args[5].std_str());
            int64 z2 = std::stoi(args[6].std_str());
            Str block_type = args[7];

            if (not is_valid_block_type(block_type)) {
                output = format{} << "Invalid block: '" << block_type;
                log<LogType::ERROR>(output);
                return output;
            }

            int64 min_x = std::min(x1, x2);
            int64 max_x = std::max(x1, x2);
            int64 min_y = std::min(y1, y2);
            int64 max_y = std::max(y1, y2);
            int64 min_z = std::min(z1, z2);
            int64 max_z = std::max(z1, z2);

            if (not is_valid_coordinate(min_x, min_y, min_z) or not is_valid_coordinate(max_x, max_y, max_z)) {
                output = format{} << "(" << min_x << "," << min_x << "," << min_z << ") or (" << max_x << ", " << max_y << ", " << max_z << ") outs of bounds";
                log<LogType::ERROR>(output);
                return output;
            }

            int block_count = 0;
            for (int x = min_x; x <= max_x; ++x) {
                for (int y = min_y; y <= max_y; ++y) {
                    for (int z = min_z; z <= max_z; ++z) {
                        world->set_global_block_id(BlockRegistry::get_id(block_type), x, y, z);
                        block_count++;
                    }
                }
            }
            output = format{} << "Filled " << block_count << " block " << block_type << " from (" << min_x << "," << min_y << "," << min_z << ") to (" << max_x << "," << max_y << "," << max_z << ")";
            log<LogType::INFO>(output);
        }
        catch (const std::exception& e) {
            output = "Invalid command arguments";
            log<LogType::ERROR>(output);
        }
        return output;
    }

    Str CommandInterpreter::execute_give(const std::vector<Str>& args) {
        TCPServer* world = static_cast<TCPServer*>(world_ptr);
        if (not world) return "";
        Str output;

        if (args.size() < 3) {
            output = "Must fill atleast (player_name item_name)";
            log<LogType::ERROR>(output);
            return output;
        }

        int64 amount = 1;
        if (args.size() >= 4) {
            try {
                amount = std::stoi(args[3].std_str());
                if (amount <= 0) {
                    output = "Amount cannot be negative";
                    log<LogType::ERROR>(output);
                    return output;
                }
                if (amount > 64) {
                    output = "Maximum for a stack is 64\n";
                    log<LogType::WARNING>(output);
                }
            }
            catch (const std::exception& e) {
                output = "Invalid command arguments";
                log<LogType::ERROR>(output);
                return output;
            }
        }

        if (not is_valid_block_type(args[2])) {
            output = format{} << "Invalid block: '" << args[2];
            log<LogType::ERROR>(output);
            return output;
        }

        PlayerData& player = world->players[args[1]];
        player.hotbar[player.selected_slot] = BlockRegistry::get_id(args[2]);

        output += format{} << "Gave " << args[1] << " " << amount << " " << args[2] << "(s)";
        log<LogType::INFO>(output);
        return output;
    }
}