module game.block.metadata;

namespace craftbuild {
    void MetaRegistry::register_metadata(Str const& name) {
        registry.emplace_back(name);
        name2id[name] = uint64(registry.size() - 1);
    }

    Str& MetaRegistry::get_metadata(uint64 meta_id) {
        if (registry.size() <= meta_id) return registry[get_id("Air")];
        return registry[meta_id];
    }

    uint64 MetaRegistry::get_id(Str const& meta_name) {
        if (name2id.find(meta_name) == name2id.end()) return 0;
        return name2id[meta_name];
    }

    bool MetaRegistry::has_metadata(Str const& meta_name) { return name2id.contains(meta_name); }
}