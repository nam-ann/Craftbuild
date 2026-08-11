export module game.command;

import std;

import misc.pos;
import misc.str;
import misc.range;
import misc.format;
import misc.number;
import game.block;
import game.logger;
import game.player;

namespace craftbuild {
    inline Str trim(Str const& str);
    inline std::vector<Str> tokenize_with_quotes(Str const& input);
}

export namespace craftbuild {
    class CommandInterpreter {
    private:
        void* world_ptr = nullptr;

        bool is_valid_coordinate(int64 x, int64 y, int64 z);
        bool is_valid_block_type(Str const& block_type);

    public:
        CommandInterpreter(void* world) : world_ptr(world) {}

        Str execute_command(Str const& command_line);
        Str execute_set_block(std::vector<Str> const& args);
        Str execute_fill(std::vector<Str> const& args);
        Str execute_give(std::vector<Str> const& args);
    };
}