module;

#include <includes.hpp>
#include <unordered_map>

module game.world.cave;

namespace craftbuild {
    void CaveRegistry::register_cave(Str const& name, Cave cave) {
        name2id[name] = registry.size();
        registry.emplace_back(name, cave);
    }

    Cave CaveRegistry::get_cave(uint64 cave_id) {
        if (registry.size() <= cave_id) return Cave{};
        return registry[cave_id].cave;
    }

    Str CaveRegistry::get_name(uint64 cave_id) {
        if (registry.size() <= cave_id) return "";
        return registry[cave_id].name;
    }

    uint64 CaveRegistry::get_id(Str const& cave_name) {
        if (name2id.find(cave_name) == name2id.end()) return 0;
        return name2id[cave_name];
    }
}