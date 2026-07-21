module;

#pragma warning(push, 0)
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#pragma warning(pop)

#include <includes.hpp>
#include <functional>

module game.block;

namespace craftbuild {
    TagEntry::TagEntry() = default;
	TagEntry::TagEntry(Str  const& n) : name(n) {};

	uint32 TagRegistry::register_tag(Str const& name) {
        tag.push_back(name);
        const uint32 tag_size = (uint32)(tag.size() - 1);
        tag2id[name] = tag_size;
        return tag_size;
    }

    uint64 TagRegistry::add_value(uint32 tag_id, uint64 value) {
        if (tag_id >= tag.size()) return 0;
        auto& values = tag[tag_id].value;
        values.append(value);
        return len(values) - 1;
    }

    none TagRegistry::set_value(uint32 tag_id, uint64 index, uint64 value) {
        if (tag_id >= tag.size()) return;
        auto& values = tag[tag_id].value;
        if (index >= len(values)) add_value(tag_id, value);
        else values[index] = value;
    }

    List<uint64>& TagRegistry::get_value(uint32 tag_id) { return tag[tag_id].value; }
    uint64& TagRegistry::get_value(uint32 tag_id, uint64 index) { return tag[tag_id].value[index]; }
    Str TagRegistry::get_name(uint32 tag_id) { return tag[tag_id].name; }

    uint32 TagRegistry::get_id(Str const& tag_name) {
        if (tag2id.find(tag_name) == tag2id.end()) return 0;
        return tag2id[tag_name];
    }

    Block::~Block() = default;
    std::vector<std::pair<Str, uint64>> Block::init_tags() { return {}; }

    int32 Block1F::get_texture_layer(Face face) const {
        return base_texture_layer;
    }

    int32 Block3F::get_texture_layer(Face face) const {
        if (face == Face::TOP)    return base_texture_layer;
        if (face == Face::BOTTOM) return base_texture_layer + 1;
        return base_texture_layer + 2;
    }

    int32 Block6F::get_texture_layer(Face face) const {
        switch (face) {
        case Face::TOP:    return base_texture_layer;
        case Face::BOTTOM: return base_texture_layer + 1;
        case Face::LEFT:   return base_texture_layer + 2;
        case Face::RIGHT:  return base_texture_layer + 3;
        case Face::FRONT:  return base_texture_layer + 4;
        case Face::BACK:   return base_texture_layer + 5;
        }
        return base_texture_layer;
    }

    int32 DynBlock::get_texture_layer(Face face) const { return -1; }

    BlockEntry::BlockEntry(Ptr<Block>&& b, Str const& n, Ref<Texture2D> const& t, Ref<Mesh> const& m) : block(b), name(n), texture(t), mesh(m) {}

    Ptr<Block>& BlockRegistry::get_block(uint32 block_id) {
        if (registry.size() <= block_id) return registry[get_id("Air")].block;
        return registry[block_id].block;
    }

    Str BlockRegistry::get_name(uint32 block_id) {
        if (registry.size() <= block_id) return "Air";
        return registry[block_id].name;
    }

    uint32 BlockRegistry::get_id(Str const& block_name) {
        if (name2id.find(block_name) == name2id.end()) return 0;
        return name2id[block_name];
    }

    bool BlockRegistry::has_block(Str const& block_name) { return name2id.contains(block_name); }
}