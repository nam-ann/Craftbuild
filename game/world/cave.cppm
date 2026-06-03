module;

#include <includes.hpp>
#include <unordered_map>

export module game.world.cave;

import misc.str;
import misc.dict;
import misc.number;

export namespace craftbuild {
    enum class CaveType {
        CHEESE,
        SPAGHETTI,
        NOODLE
    };

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
        inline static Dict<Str, size> name2id;

        static none register_cave(const Str& name, Cave cave) {
            name2id[name] = registry.size();
            registry.emplace_back(name, cave);
        }

        static Cave get_cave(size cave_id) {
            if (registry.size() <= cave_id) return Cave{};
            return registry[cave_id].cave;
        }

        static Str get_name(size cave_id) {
            if (registry.size() <= cave_id) return "";
            return registry[cave_id].name;
        }

        static size get_id(const Str& cave_name) {
            if (name2id.find(cave_name) == name2id.end()) return 0;
            return name2id[cave_name];
        }
    };
}