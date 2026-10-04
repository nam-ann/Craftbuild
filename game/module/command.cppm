export module game.command;

import std;

import misc.pos;
import misc.str;
import misc.list;
import misc.range;
import misc.format;
import misc.number;
import game.block;
import game.logger;
import game.player;

namespace craftbuild {
    inline Str trim(Str const& str);
    inline List<Str> tokenize_with_quotes(Str const& input);
}

export namespace craftbuild {
    class CommandInterpreter final {
    private:
        void* world_ptr = nullptr;

        bool is_valid_coordinate(i64 x, i64 y, i64 z);
        bool is_valid_block_type(Str const& block_type);

    public:
        CommandInterpreter(void* world) : world_ptr(world) {}

        Str execute_command(Str const& command_line);
        Str execute_set_block(List<Str> const& args);
        Str execute_fill(List<Str> const& args);
        Str execute_give(List<Str> const& args);
    };
}