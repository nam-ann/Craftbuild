module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/classes/texture2d.hpp>
ENABLE_WARNING

module game.block;

namespace craftbuild {
    Block::~Block() = default;
    Set<u32> Block::init_tags() { return {}; }
    Dict<u32, Str> Block::init_metadatas() { return {}; }

    i32 Block1F::get_texture_layer(Face face) const {
        return base_texture_layer;
    }

    i32 Block3F::get_texture_layer(Face face) const {
        if (face == Face::TOP)    return base_texture_layer;
        if (face == Face::BOTTOM) return base_texture_layer + 1;
        return base_texture_layer + 2;
    }

    i32 Block6F::get_texture_layer(Face face) const {
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

    i32 ComplexBlock::get_texture_layer(Face face) const { return -1; }

    BlockEntry::BlockEntry(Ptr<Block>&& b, Str const& n, Ref<Texture2D> const& t, Ref<Mesh> const& m) : block(b), name(n), texture(t), mesh(m) {}

    namespace block_registry {
        Block& get_block(u32 block_id) {
            if (len(registry) <= block_id) return registry[get_id("Air")].block.value();
            return registry[block_id].block.value();
        }

        Str get_name(u32 block_id) {
            if (len(registry) <= block_id) return "Air";
            return registry[block_id].name;
        }

        u32 get_id(Str const& block_name) {
            if (not name2id.contains(block_name)) return 0;
            return name2id[block_name];
        }

        bool has_block(Str const& block_name) { return name2id.contains(block_name); }
    }
}