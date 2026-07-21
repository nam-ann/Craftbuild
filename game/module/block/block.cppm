module;

#pragma warning(push, 0)
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
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
import game.logger;
import game.texture.asset_loader;

using namespace godot;

export namespace craftbuild {
    inline constexpr uint8 FACE_LEN = 6;
    enum class Face : uint8 { TOP, BOTTOM, RIGHT, LEFT, FRONT, BACK };

    struct TagEntry {
        Str name;
        List<uint64> value;

        TagEntry();
        TagEntry(Str const& n);
    };

    struct TagRegistry {
        inline static std::vector<TagEntry> tag;
        inline static Dict<Str, uint32> tag2id;

        static uint32 register_tag(Str const& name);
        static uint64 add_value(uint32 tag_id, uint64 value);
        static void set_value(uint32 tag_id, uint64 index, uint64 value);
        static List<uint64>& get_value(uint32 tag_id);
        static uint64& get_value(uint32 tag_id, uint64 index);
        static Str get_name(uint32 tag_id);
        static uint32 get_id(Str const& tag_name);
    };

    class Block {
    protected:
        int32 base_texture_layer = 0;

    public:
        virtual ~Block();
        virtual int32 get_texture_layer(Face face) const = 0;
        virtual std::vector<std::pair<Str, uint64>> init_tags();

        friend class AtlasTexture;
    };

    struct Block1F : Block { int32 get_texture_layer(Face face) const override final; };
    struct Block3F : Block { int32 get_texture_layer(Face face) const override final; };
    struct Block6F : Block { int32 get_texture_layer(Face face) const override final; };
    class DynBlock : public Block {
        int32 get_texture_layer(Face face) const override final;
    };

    struct BlockEntry {
        Ref<Texture2D> texture = nullptr;
        Ptr<Block> block = nullptr;
        Ref<Mesh> mesh;
        Str name;

        BlockEntry(Ptr<Block>&& b, Str const& n, Ref<Texture2D> const& t, Ref<Mesh> const& m);
    };

    struct BlockRegistry {
        inline static std::vector<BlockEntry> registry;
        inline static Dict<Str, uint32> name2id;

        template <typename T>
        requires std::derived_from<T, Block>
        static void register_block(Str const& name, char const* path) {
            Ptr<Block> block = new Obj<T>();
            Ref<Texture2D> texture = nullptr;
            Ref<Mesh> mesh = nullptr;

            if constexpr (std::derived_from<T, Block1F>) {
                texture = AssetLoader::load_block_texture(path, FaceCount::ONE);
            }
            else if constexpr (std::derived_from<T, Block3F>) {
                texture = AssetLoader::load_block_texture(path, FaceCount::THREE);
            }
            else if constexpr (std::derived_from<T, Block6F>) {
                texture = AssetLoader::load_block_texture(path, FaceCount::SIX);
            }
            else if constexpr (std::derived_from<T, DynBlock>) {
                Ref<PackedScene> glb_model = AssetLoader::load_block_model(path);

                Node* root = glb_model->instantiate();
                TypedArray<Node> mesh_children = root->find_children("*", "MeshInstance3D", true, false);
                MeshInstance3D* mesh_inst = mesh_children.is_empty() ? nullptr : Object::cast_to<MeshInstance3D>(mesh_children[0]);

                mesh = mesh_inst->get_mesh();
            }

            registry.emplace_back(std::move(block), name, texture, mesh);
            name2id[name] = (uint32)(registry.size() - 1);
        }

        static Ptr<Block>& get_block(uint32 block_id);
        static Str get_name(uint32 block_id);
        static uint32 get_id(Str const& block_name);
        static bool has_block(Str const& block_name);
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