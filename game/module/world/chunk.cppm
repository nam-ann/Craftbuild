module;

#pragma warning(push, 0)
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#pragma warning(pop)

#include <includes.hpp>
#include <mutex>
#include <algorithm>
#include <shared_mutex>
#include <unordered_set>

export module game.world.chunk;

import misc.gc;
import misc.ptr;
import misc.str;
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

using namespace godot;

export namespace craftbuild {
    struct DynBlockInstance {
        uint32 block_id;
        uint64 tag_data;
        Pos3D<uint8> local_pos;
    };

    struct MeshData {
        List<Pos3D<real>> vertices;
        List<Pos3D<real>> normals;
        List<int32> indices;
        List<Pos2D<real>> uvs;
        List<Pos2D<real>> uvs_layer;
        List<Pos3D<real>> collision_faces;
        List<DynBlockInstance> dyn_instances;
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

        ChunkRender() noexcept;
        ChunkRender(ChunkRender&& other) noexcept;
        void clear();
    };

    class Chunk {
    public:
        inline static constexpr uint8 SIZE_X = 16;
        inline static constexpr uint8 SIZE_Y = 255;
        inline static constexpr uint8 SIZE_Z = 16;

        uint32 block_ids[256];
        std::pair<uint32, uint64> tag_ids[256];
        uint8 block_ids_size = 0;
        uint8 tag_ids_size = 0;

        Dict<uint32, uint8> id2block;
        Dict<std::pair<uint32, uint64>, uint8> id2tag;

        Dict<Pos3D<uint8>, BlockStorageFull> complex_blocks;
        BlockStorage blocks[SIZE_X][SIZE_Y][SIZE_Z] = {};

        Pos3D<int32> chunk_pos;
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

        void set_block(Pos3D<uint8>& pos, Str const& block);
        void set_block(Pos3D<uint8> const& pos, uint32 block_id);

        void tag_block(Pos3D<uint8>& pos, Str const& tag, usize tag_data = 0);
        void tag_block(Pos3D<uint8> const& pos, uint32 tag_id, usize tag_data = 0);

        bool has_tag(Pos3D<uint8>& pos, Str const& tag, usize tag_data = 0) const;
        bool has_tag(Pos3D<uint8> const& pos, uint32 tag_id, usize tag_data = 0) const;

        uint32 get_block(Pos3D<uint8> const& pos) const;
        std::pair<uint32, uint64> get_tag(Pos3D<uint8> const& pos) const;

        void generate_terrain(int32 seed, Ref<FastNoiseLite> noise);
        void generate_mesh(ChunkRender& mesh, Ptr<Chunk> neighbors[4]);
    };
}
