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
import misc.ptr;
import misc.str;
import misc.set;
import misc.dict;
import misc.list;
import misc.range;
import misc.number;
import misc.pos;
import game.block;
import game.logger;
import game.world.cave;
import game.world.biome;
import game.world.terrain;
import game.block.block_data;

using namespace godot;

export namespace craftbuild {
    struct ComplexBlockInstance {
        uint32 block_id;
        Pos3D<uint8> local_pos;
    };

    struct MeshData {
        List<Pos3D<real>> vertices;
        List<Pos3D<real>> normals;
        List<int32> indices;
        List<Pos2D<real>> uvs;
        List<Pos2D<real>> uvs_layer;
        List<Pos3D<real>> collision_faces;
        List<ComplexBlockInstance> complex_instance;
    };

    struct FaceMask {
        int32 layer = -1;
        bool back_face = false;

        bool operator==(FaceMask const& other) const;
    };

    struct ChunkRender {
        MeshInstance3D* mesh_instance = nullptr;
        StaticBody3D* collision_body = nullptr;
        CollisionShape3D* collision_shape = nullptr;

        MultiMeshInstance3D* multi_mesh_instance = nullptr;
        StaticBody3D* dynamic_body = nullptr;

        Ptr<MeshData> pending_mesh_data = nullptr;
        mutable std::shared_mutex mesh_mutex;

        ~ChunkRender() noexcept;
        void clear();
    };

    class Chunk {
    public:
        inline static constexpr uint8 WIDTH  = 16;
        inline static constexpr uint8 HEIGHT = 255;

        uint32 block_ids[256];
        uint8 block_ids_size = 0;

        Dict<uint32, uint8> id2block;

        Dict<Pos3D<uint8>, Set<uint32>> tag_ids;
        Dict<Pos3D<uint8>, Dict<uint32, Str>> meta_ids;
        Dict<Pos3D<uint8>, uint32> extended_block_id;
        uint8 blocks[WIDTH][HEIGHT][WIDTH] = {};

        Pos2D<int32> chunk_pos;
        TrapezoidHeight height_provider{ VerticalAnchor::absolute(18), VerticalAnchor::absolute(38), 8 };
        std::atomic<bool> generated = false;
        std::atomic<bool> dirty = true;
        mutable std::shared_mutex data_mutex;

        uint8 chunk_version = 0;

        void clear();

        static uint32 column_seed(int32 seed, int32 x, int32 z);
        static float32 smoothstep(float32 value);
        static Biome lerp_biome(Biome const& a, Biome const& b, float32 t);
        static Biome select_biome_at(int32 wx, int32 wz, Ref<FastNoiseLite> noise, usize biome_count);
        static Biome get_blended_biome(int32 wx, int32 wz, Ref<FastNoiseLite> noise, usize biome_count);

        void set_block(Pos3D<uint8> const& pos, Str const& block);
        void set_block(Pos3D<uint8> const& pos, uint32 block_id);

        void tag_block(Pos3D<uint8> const& pos, Str const& tag);
        void tag_block(Pos3D<uint8> const& pos, uint32 tag_id);

        void set_block_metadata(Pos3D<uint8> const& pos, Str const& meta, Str const& meta_data = "");
        void set_block_metadata(Pos3D<uint8> const& pos, uint32 meta_id, Str const& meta_data = "");

        bool has_tag(Pos3D<uint8> const& pos, Str const& tag) const;
        bool has_tag(Pos3D<uint8> const& pos, uint32 tag_id) const;

        bool has_metadata(Pos3D<uint8> const& pos, Str const& meta, Str const& meta_data = "") const;
        bool has_metadata(Pos3D<uint8> const& pos, uint32 meta_id, Str const& meta_data = "") const;

        uint32 get_block(Pos3D<uint8> const& pos) const;
        Set<uint32> const* get_tag(Pos3D<uint8> const& pos) const;
        Dict<uint32, Str> const* get_metadata(Pos3D<uint8> const& pos) const;

        void generate_terrain(int32 seed, Ref<FastNoiseLite> noise);
        void generate_mesh(ChunkRender& mesh, Ptr<Chunk> neighbors[4]);
    };
}
