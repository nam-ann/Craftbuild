module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
ENABLE_WARNING

export module game.block;

import std;

import misc.ptr;
import misc.str;
import misc.set;
import misc.dict;
import misc.list;
import misc.number;
import misc.pos;
import game.core;
import game.logger;
import game.block.block_data;
import game.texture.asset_loader;

using namespace godot;

export namespace craftbuild {
    inline constexpr uint8 FACE_LEN = 6;
    enum class Face : uint8 { TOP, BOTTOM, RIGHT, LEFT, FRONT, BACK };

    class Block {
    protected:
        int32 base_texture_layer = 0;

    public:
        virtual ~Block();
        virtual int32 get_texture_layer(Face face) const = 0;
        virtual Set<uint32> init_tags();
        virtual Dict<uint32, Str> init_metadatas();

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

            registry.emplace_back(new Obj<T>(), name, texture, mesh);
            name2id[name] = uint32(registry.size() - 1);
        }

        static Ptr<Block>& get_block(uint32 block_id);
        static Str get_name(uint32 block_id);
        static uint32 get_id(Str const& block_name);
        static bool has_block(Str const& block_name);
    };
}