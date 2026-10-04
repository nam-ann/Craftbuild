module game.block.block_data;

namespace craftbuild {
    namespace meta_registry {
        void register_metadata(Str const& name) {
            registry.emplace(name);
            name2id[name] = u32(len(registry) - 1);
        }

        Str& get_metadata(u32 meta_id) {
            if (len(registry) <= meta_id) return registry[get_id("Air")];
            return registry[meta_id];
        }

        u32 get_id(Str const& meta_name) {
            if (not name2id.contains(meta_name)) return 0;
            return name2id[meta_name];
        }

        bool has_metadata(Str const& meta_name) { return name2id.contains(meta_name); }
    }

    namespace tag_registry {
        void register_tag(Str const& name) {
            registry.emplace(name);
            name2id[name] = u32(len(registry) - 1);
        }

        Str& get_tag(u32 tag_id) {
            if (len(registry) <= tag_id) return registry[get_id("Air")];
            return registry[tag_id];
        }

        u32 get_id(Str const& tag_name) {
            if (not name2id.contains(tag_name)) return 0;
            return name2id[tag_name];
        }

        bool has_tag(Str const& tag_name) { return name2id.contains(tag_name); }
    }
}