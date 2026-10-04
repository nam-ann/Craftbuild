export module game.world.cave;

import std;

import misc.str;
import misc.list;
import misc.dict;
import misc.number;

export namespace craftbuild {
    enum class CaveType { CHEESE, SPAGHETTI, NOODLE };

    struct Cave final {
        CaveType cave_type;
        f32 threshold;
        f32 frequency;
    };

    struct CaveEntry final {
        Str name;
        Cave cave;
    };

    namespace cave_registry {
        inline List<CaveEntry> registry;
        inline Dict<Str, u64> name2id;

        void register_cave(Str const& name, Cave cave);
        Cave get_cave(u64 cave_id);
        Str get_name(u64 cave_id);
        u64 get_id(Str const& cave_name);
    };
}