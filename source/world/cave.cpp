module;

#include <includes.hpp>
#include <unordered_map>

module game.world.cave;

namespace craftbuild {
    none CaveRegistry::register_cave(const Str& name, Cave cave) {
        name2id[name] = registry.size();
        registry.emplace_back(name, cave);
    }

    Cave CaveRegistry::get_cave(size cave_id) {
        if (registry.size() <= cave_id) return Cave{};
        return registry[cave_id].cave;
    }

    Str CaveRegistry::get_name(size cave_id) {
        if (registry.size() <= cave_id) return "";
        return registry[cave_id].name;
    }

    size CaveRegistry::get_id(const Str& cave_name) {
        if (name2id.find(cave_name) == name2id.end()) return 0;
        return name2id[cave_name];
    }
}