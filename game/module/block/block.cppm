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
import misc.pos;
import misc.str;
import misc.set;
import misc.list;
import misc.dict;
import misc.list;
import misc.number;
import game.core;
import game.logger;
import game.block.block_data;
import game.texture.asset_loader;

using namespace godot;

namespace craftbuild::atlas_texture { void build_texture_array(); }

export namespace craftbuild {
    inline constexpr u8 FACE_LEN = 6;
    enum class Face : u8 { TOP, BOTTOM, RIGHT, LEFT, FRONT, BACK };

    class Block {
    protected:
        i32 base_texture_layer = 0;

    public:
        virtual ~Block();
        virtual i32 get_texture_layer(Face face) const = 0;
        virtual Set<u32> init_tags();
        virtual Dict<u32, Str> init_metadatas();

        friend void atlas_texture::build_texture_array();
    };

    struct Block1F : Block { i32 get_texture_layer(Face face) const override final; };
    struct Block3F : Block { i32 get_texture_layer(Face face) const override final; };
    struct Block6F : Block { i32 get_texture_layer(Face face) const override final; };
    struct ComplexBlock : Block { i32 get_texture_layer(Face face) const override final; };

    struct BlockEntry final {
        Ref<Texture2D> texture = nullptr;
        Ptr<Block> block = nullptr;
        Ref<Mesh> mesh;
        Str name;

        BlockEntry(Ptr<Block>&& b, Str const& n, Ref<Texture2D> const& t, Ref<Mesh> const& m);
    };

    namespace block_registry {
        inline List<BlockEntry> registry;
        inline Dict<Str, u32> name2id;

        template <typename T>
        requires std::derived_from<T, Block>
        void register_block(Str const& name, char const* path) {
            Ref<Texture2D> texture = nullptr;
            Ref<Mesh> mesh = nullptr;

            if constexpr (std::derived_from<T, Block1F>) {
                texture = asset_loader::load_block_texture(path, FaceCount::ONE);
            }
            else if constexpr (std::derived_from<T, Block3F>) {
                texture = asset_loader::load_block_texture(path, FaceCount::THREE);
            }
            else if constexpr (std::derived_from<T, Block6F>) {
                texture = asset_loader::load_block_texture(path, FaceCount::SIX);
            }
            else if constexpr (std::derived_from<T, ComplexBlock>) {
                Ref<PackedScene> glb_model = asset_loader::load_block_model(path);

                if (glb_model.is_valid()) {
                    Node* root = glb_model->instantiate();
                    TypedArray<Node> mesh_children = root->find_children("*", "MeshInstance3D", true, false);
                    MeshInstance3D* mesh_inst = mesh_children.is_empty() ? nullptr : Object::cast_to<MeshInstance3D>(mesh_children[0]);

                    mesh = mesh_inst->get_mesh();
                }
            }

            registry.emplace(new Obj<T>(), name, texture, mesh);
            name2id[name] = u32(len(registry) - 1);
        }

        Block& get_block(u32 block_id);
        Str get_name(u32 block_id);
        u32 get_id(Str const& block_name);
        bool has_block(Str const& block_name);
    }
}