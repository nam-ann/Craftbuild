module;

#pragma warning(push, 0)
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#pragma warning(pop)

#include <includes.hpp>

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
    inline constexpr uint8 FACE_LEN = 6;
    enum class Face : uint8 { TOP, BOTTOM, RIGHT, LEFT, FRONT, BACK };

    struct TagEntry {
        Str name;
        List<uint64> value;

        TagEntry();
        TagEntry(const Str& n);
    };

    struct TagRegistry {
        inline static std::vector<TagEntry> tag;
        inline static Dict<Str, uint32> tag2id;

        static uint32 register_tag(const Str& name);
        static uint64 add_value(uint32 tag_id, uint64 value);
        static none set_value(uint32 tag_id, uint64 index, uint64 value);
        static List<uint64>& get_value(uint32 tag_id);
        static uint64& get_value(uint32 tag_id, uint64 index);
        static Str get_name(uint32 tag_id);
        static uint32 get_id(const Str& tag_name);
    };

    class Block {
    protected:
        int32 base_texture_layer = 0;

    public:
        virtual ~Block();
        virtual int32 get_texture_layer(Face face) const = 0;
        virtual std::vector<std::pair<Str, uint64>> init_tags();

        static none create_face(Face face, const Vector3& pos, List<Pos3D<real>>& vertices);
        friend class AtlasTexture;
    };

    struct Block1F : Block { int32 get_texture_layer(Face face) const override final; };
    struct Block3F : Block { int32 get_texture_layer(Face face) const override final; };
    struct Block6F : Block { int32 get_texture_layer(Face face) const override final; };

    struct BlockEntry {
        Ref<Texture2D> texture = nullptr;
        Ptr<Block> block = nullptr;
        Str name;

        BlockEntry(Ptr<Block>&& b, const Str& n, Ref<Texture2D> t);
    };

    struct BlockRegistry {
        inline static std::vector<BlockEntry> registry;
        inline static Dict<Str, uint32> name2id;

        template <typename T>
        requires std::derived_from<T, Block>
        static none register_block(const Str& name, const char* path) {
            Ptr<Block> block = new Obj<T>();
            Ref<Texture2D> texture;

            if constexpr (std::derived_from<T, Block1F>) {
                texture = AssetLoader::load_block_texture(registry.size(), path, FaceCount::ONE);
            }
            else if constexpr (std::derived_from<T, Block3F>) {
                texture = AssetLoader::load_block_texture(registry.size(), path, FaceCount::THREE);
            }
            else if constexpr (std::derived_from<T, Block6F>) {
                texture = AssetLoader::load_block_texture(registry.size(), path, FaceCount::SIX);
            }

            registry.emplace_back(std::move(block), name, texture);
            name2id[name] = (uint32)(registry.size() - 1);
        }

        static Ptr<Block>& get_block(uint32 block_id);
        static Str get_name(uint32 block_id);
        static uint32 get_id(const Str& block_name);
        static bool has_block(const Str& block_name);
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