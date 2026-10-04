module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
ENABLE_WARNING

export module game.world.chunk;

import std;

import misc.gc;
import misc.pos;
import misc.ptr;
import misc.str;
import misc.set;
import misc.dict;
import misc.list;
import misc.range;
import misc.number;
import game.block;
import game.logger;
import game.world.cave;
import game.world.biome;
import game.world.terrain;
import game.block.block_data;

using namespace godot;

export namespace craftbuild {
    struct ComplexBlockInstance final {
        u32 block_id;
        Pos3D<u8> local_pos;
    };

    struct MeshData final {
        List<Pos3D<fsize>> vertices;
        List<Pos3D<fsize>> normals;
        List<i32> indices;
        List<Pos2D<fsize>> uvs;
        List<Pos2D<fsize>> uvs_layer;
        List<Pos3D<fsize>> collision_faces;
        List<ComplexBlockInstance> complex_instance;
    };

    class MeshesData final {
        MeshData sub[4];

    public:
		auto& operator[](u8 idx) { return sub[idx]; }
    };

    class FaceMask final {
        u32 value = 0;

    public:
        static constexpr u32 BACK_FACE_BIT = 0x80000000u;

        constexpr FaceMask();
        constexpr FaceMask(i32 layer, bool back_face);

        i32 layer() const;
        bool back_face() const;

        bool operator==(FaceMask const& other) const;
    };

    struct ChunkRender final {
        MeshInstance3D* mesh_instances[4] = {};
        StaticBody3D* collision_body = nullptr;
        CollisionShape3D* collision_shape = nullptr;

        MultiMeshInstance3D* multi_mesh_instance = nullptr;
        StaticBody3D* dynamic_body = nullptr;

        Ptr<MeshesData> pending_meshes_data = nullptr;
        mutable std::shared_mutex mesh_mutex;

        ~ChunkRender() noexcept;
        void clear();
    };

    class Chunk final {
    public:
        inline static constexpr u8 WIDTH  = 16;
        inline static constexpr u8 HEIGHT = 255;

        u32 block_ids[256];
        u8 block_ids_size = 0;

        Dict<u32, u8> id2block;

        Dict<Pos3D<u8>, Set<u32>> tag_ids;
        Dict<Pos3D<u8>, Dict<u32, Str>> meta_ids;
        Dict<Pos3D<u8>, u32> extended_block_id;
        u8 blocks[WIDTH][HEIGHT][WIDTH] = {};

        Pos2D<i32> chunk_pos;
        TrapezoidHeight height_provider{ VerticalAnchor::absolute(18), VerticalAnchor::absolute(38), 8 };
        std::atomic<bool> generated = false;
        std::atomic<bool> dirty = true;
        mutable std::shared_mutex data_mutex;

        u8 chunk_version = 0;

        void clear();

        static u32 column_seed(i32 seed, i32 x, i32 z);
        static f32 smoothstep(f32 value);
        static Biome lerp_biome(Biome const& a, Biome const& b, f32 t);
        static Biome select_biome_at(i32 wx, i32 wz, Ref<FastNoiseLite> noise, usize biome_count);
        static Biome get_blended_biome(i32 wx, i32 wz, Ref<FastNoiseLite> noise, usize biome_count);

        void set_block(Pos3D<u8> const& pos, Str const& block);
        void set_block(Pos3D<u8> const& pos, u32 block_id);

        void tag_block(Pos3D<u8> const& pos, Str const& tag);
        void tag_block(Pos3D<u8> const& pos, u32 tag_id);

        void set_block_metadata(Pos3D<u8> const& pos, Str const& meta, Str const& meta_data = "");
        void set_block_metadata(Pos3D<u8> const& pos, u32 meta_id, Str const& meta_data = "");

        bool has_tag(Pos3D<u8> const& pos, Str const& tag) const;
        bool has_tag(Pos3D<u8> const& pos, u32 tag_id) const;

        bool has_metadata(Pos3D<u8> const& pos, Str const& meta, Str const& meta_data = "") const;
        bool has_metadata(Pos3D<u8> const& pos, u32 meta_id, Str const& meta_data = "") const;

        u32 get_block(Pos3D<u8> const& pos) const;
        Set<u32> const* get_tag(Pos3D<u8> const& pos) const;
        Dict<u32, Str> const* get_metadata(Pos3D<u8> const& pos) const;

        void generate_terrain(i32 seed, Ref<FastNoiseLite> noise);
        void generate_mesh(ChunkRender& mesh, Ptr<Chunk> neighbors[4]);
    };
}
