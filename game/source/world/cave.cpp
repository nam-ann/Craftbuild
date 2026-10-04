module game.world.cave;

namespace craftbuild {
    namespace cave_registry {
        void register_cave(Str const& name, Cave cave) {
            name2id[name] = len(registry);
            registry.emplace(name, cave);
        }

        Cave get_cave(u64 cave_id) {
            if (len(registry) <= cave_id) return Cave{};
            return registry[cave_id].cave;
        }

        Str get_name(u64 cave_id) {
            if (len(registry) <= cave_id) return "";
            return registry[cave_id].name;
        }

        u64 get_id(Str const& cave_name) {
            if (name2id.find(cave_name) == name2id.end()) return 0;
            return name2id[cave_name];
        }
    }
}