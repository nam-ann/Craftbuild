module;

#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include <includes.hpp>
#include <functional>

export module game.block;

import misc.ptr;
import misc.str;
import misc.dict;
import misc.list;
import misc.number;
import misc.pos;
import game.core;
import game.texture.asset_loader;

using namespace godot;

export namespace craftbuild {
    constexpr uint8 FACE_LEN = 6;

    enum class Face : uint8 {
        TOP,
        BOTTOM,
        RIGHT,
        LEFT,
        FRONT,
        BACK
    };

    struct TagEntry {
        Str name;
        List<uint64> value;

        TagEntry() = default;
        TagEntry(const Str& n) : name(n) {}
    };

    struct TagRegistry {
        inline static std::vector<TagEntry> tag;
        inline static Dict<Str, uint32> tag2id;

        static uint32 register_tag(const Str& name) {
            tag.push_back(name);
            const uint32 tag_size = (uint32)(tag.size() - 1);
            tag2id[name] = tag_size;
            return tag_size;
        }

        static size add_value(uint32 tag_id, uint64 value) {
            if (tag_id >= tag.size()) return 0;
            auto& values = tag[tag_id].value;
            values.append(value);
            return len(values) - 1;
        }

        static none set_value(uint32 tag_id, size index, uint64 value) {
			if (tag_id >= tag.size()) return;
            auto& values = tag[tag_id].value;
            if (index >= len(values)) add_value(tag_id, value);
            else values[index] = value;
        }

        static List<uint64>& get_value(uint32 tag_id) {
            return tag[tag_id].value;
        }

        static uint64& get_value(uint32 tag_id, size index) {
            return tag[tag_id].value[index];
        }

        static Str get_name(uint32 tag_id) {
            return tag[tag_id].name;
        }

        static uint32 get_id(const Str& tag_name) {
            if (tag2id.find(tag_name) == tag2id.end()) return 0;
            return tag2id[tag_name];
        }
    };

    class Block {
    protected:
        int base_texture_layer = 0;

    public:
        virtual ~Block() = default;
        virtual int get_texture_layer(Face face) const = 0;

		virtual std::vector<std::pair<Str, size>> init_tags() { return {}; }

        static none create_face(Face face, const Vector3& pos, List<Pos3D<real>>& vertices) {
            switch (face) {
            case Face::TOP: // +Y
                vertices.append(pos + Vector3(1, 1, 0));
                vertices.append(pos + Vector3(0, 1, 0));
                vertices.append(pos + Vector3(0, 1, 1));
                vertices.append(pos + Vector3(1, 1, 1));
                break;
            case Face::BOTTOM: // -Y
                vertices.append(pos + Vector3(1, 0, 1));
                vertices.append(pos + Vector3(0, 0, 1));
                vertices.append(pos + Vector3(0, 0, 0));
                vertices.append(pos + Vector3(1, 0, 0));
                break;
            case Face::RIGHT: // +X
                vertices.append(pos + Vector3(1, 1, 0));
                vertices.append(pos + Vector3(1, 1, 1));
                vertices.append(pos + Vector3(1, 0, 1));
                vertices.append(pos + Vector3(1, 0, 0));
                break;
            case Face::LEFT: // -X
                vertices.append(pos + Vector3(0, 1, 1));
                vertices.append(pos + Vector3(0, 1, 0));
                vertices.append(pos + Vector3(0, 0, 0));
                vertices.append(pos + Vector3(0, 0, 1));
                break;
            case Face::FRONT: // +Z
                vertices.append(pos + Vector3(1, 1, 1));
                vertices.append(pos + Vector3(0, 1, 1));
                vertices.append(pos + Vector3(0, 0, 1));
                vertices.append(pos + Vector3(1, 0, 1));
                break;
            case Face::BACK: // -Z
                vertices.append(pos + Vector3(0, 1, 0));
                vertices.append(pos + Vector3(1, 1, 0));
                vertices.append(pos + Vector3(1, 0, 0));
                vertices.append(pos + Vector3(0, 0, 0));
                break;
            }
        }

        friend class AtlasTexture;
    };

    class Block1F : public Block {
    public:
        int get_texture_layer(Face face) const override {
            return base_texture_layer;
        }
    };
    class Block3F : public Block {
    public:
        int get_texture_layer(Face face) const override {
            if (face == Face::TOP)    return base_texture_layer;
            if (face == Face::BOTTOM) return base_texture_layer + 1;
            return base_texture_layer + 2;
        }
    };
    class Block6F : public Block {
    public:
        int get_texture_layer(Face face) const override {
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
    };

    struct BlockEntry {
        Ptr<Block> block;
        Str name;
        Ref<Texture2D> texture = nullptr;

        BlockEntry(Ptr<Block> b, const Str& n, Ref<Texture2D> t) : block(b), name(n), texture(t) {}
    };

    struct BlockRegistry {
        inline static std::vector<BlockEntry> registry;
        inline static Dict<Str, uint32> name2id;

        template <typename T>
        requires std::derived_from<T, Block>
        static none register_block(const Str& name, const char* path) {
            Ptr<Block> block = new T();
            Ref<Texture2D> texture;
            if (dynamic_cast<Block1F*>(block.c_ptr())) {
                texture = AssetLoader::load_block_texture(registry.size(), path, FaceCount::ONE);
            }
            else if (dynamic_cast<Block3F*>(block.c_ptr())) {
                texture = AssetLoader::load_block_texture(registry.size(), path, FaceCount::THREE);
            }
            else if (dynamic_cast<Block6F*>(block.c_ptr())) {
                texture = AssetLoader::load_block_texture(registry.size(), path, FaceCount::SIX);
            }
            for (const auto& pair : block.value().init_tags()) {
                TagRegistry::set_value(TagRegistry::get_id(pair.first), pair.second, 0);
            }
            registry.emplace_back(block, name, texture);
            name2id[name] = registry.size() - 1;
        }

        static Ptr<Block> get_block(uint32 block_id) {
            if (registry.size() <= block_id) return registry[get_id("Air")].block;
            return registry[block_id].block;
        }

        static Str get_name(uint32 block_id) {
            if (registry.size() <= block_id) return "Air";
            return registry[block_id].name;
        }

        static uint32 get_id(const Str& block_name) {
            if (name2id.find(block_name) == name2id.end()) return 0;
            return name2id[block_name];
        }

        static bool has_block(const Str& block_name) {
            return name2id.contains(block_name);
        }
    };

    struct BlockStorage {
        uint8 block_id;
        uint8 tag;
    };
    struct BlockStorageFull {
        uint32 block_id;
        uint32 tag;
        uint64 tag_data;
    };
}