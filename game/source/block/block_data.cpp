module game.block.block_data;

namespace craftbuild {
    void MetaRegistry::register_metadata(Str const& name) {
        registry.emplace(name);
        name2id[name] = uint32(len(registry) - 1);
    }

    Str& MetaRegistry::get_metadata(uint32 meta_id) {
        if (len(registry) <= meta_id) return registry[get_id("Air")];
        return registry[meta_id];
    }

    uint32 MetaRegistry::get_id(Str const& meta_name) {
        if (name2id.find(meta_name) == name2id.end()) return 0;
        return name2id[meta_name];
    }

    bool MetaRegistry::has_metadata(Str const& meta_name) { return name2id.contains(meta_name); }

    void TagRegistry::register_tag(Str const& name) {
        registry.emplace(name);
        name2id[name] = uint32(len(registry) - 1);
    }

    Str& TagRegistry::get_tag(uint32 tag_id) {
        if (len(registry) <= tag_id) return registry[get_id("Air")];
        return registry[tag_id];
    }

    uint32 TagRegistry::get_id(Str const& tag_name) {
        if (name2id.find(tag_name) == name2id.end()) return 0;
        return name2id[tag_name];
    }

    bool TagRegistry::has_tag(Str const& tag_name) { return name2id.contains(tag_name); }
}