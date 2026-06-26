module;

#include <includes.hpp>

export module game.world.cave;

import misc.str;
import misc.dict;
import misc.number;

export namespace craftbuild {
    enum class CaveType { CHEESE, SPAGHETTI, NOODLE };

    struct Cave {
        CaveType cave_type;
        float32 threshold;
        float32 frequency;
    };

    struct CaveEntry {
        Str name;
        Cave cave;
    };

    struct CaveRegistry {
        inline static std::vector<CaveEntry> registry;
        inline static Dict<Str, uint64> name2id;

        static none register_cave(const Str& name, Cave cave);
        static Cave get_cave(uint64 cave_id);
        static Str get_name(uint64 cave_id);
        static uint64 get_id(const Str& cave_name);
    };
}