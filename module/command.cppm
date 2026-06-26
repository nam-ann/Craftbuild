module;

#include <includes.hpp>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>
#include <cctype>

export module game.command;

import misc.pos;
import misc.str;
import misc.range;
import misc.format;
import misc.number;
import game.block;
import game.logger;
import game.player;

namespace craftbuild {
    inline Str trim(const Str& str) {
        std::string _str = str.std_str();
        size first = _str.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) return "";
        size last = _str.find_last_not_of(" \t\n\r");
        return _str.substr(first, last - first + 1);
    }

    inline std::vector<Str> tokenize_with_quotes(const Str& input) {
        std::vector<Str> tokens;
        Str current_token;
        bool in_quotes = false;

        for (auto i : range<size>(len(input))) {
            char c = input[i];

            if (c == '"') {
                if (in_quotes) {
                    in_quotes = false;
                    if (current_token) {
                        tokens.push_back(current_token);
                        current_token.clear();
                    }
                }
                else {
                    in_quotes = true;
                    if (current_token) {
                        tokens.push_back(current_token);
                        current_token.clear();
                    }
                }
            }
            else if (c == ' ' and not in_quotes) {
                if (current_token) {
                    tokens.push_back(current_token);
                    current_token.clear();
                }
            }
            else current_token += std::string(1, c);
        }

        if (current_token) tokens.push_back(current_token);

        return tokens;
    }
}

export namespace craftbuild {
    class CommandInterpreter {
    private:
        none* world_ptr = nullptr;

        bool is_valid_coordinate(int64 x, int64 y, int64 z);
        bool is_valid_block_type(const Str& block_type) {
            return BlockRegistry::has_block(block_type);
        }

    public:
        CommandInterpreter(none* world) : world_ptr(world) {}

        Str execute_command(const Str& command_line) {
            Str cmd = trim(command_line);
            if (not cmd) return "";

            std::vector<Str> parts = tokenize_with_quotes(cmd);
            if (parts.empty()) return "";

            if (parts[0] == "set_block") return execute_set_block(parts);
            else if (parts[0] == "fill") return execute_fill(parts);
            else if (parts[0] == "give") return execute_give(parts);
            else {
                Str output = format{} << "Invalid command: " << parts[0];
                log<LogType::ERROR>(output);
                return output;
            }
        }

        Str execute_set_block(const std::vector<Str>& args);
        Str execute_fill(const std::vector<Str>& args);
        Str execute_give(const std::vector<Str>& args);
    };
}