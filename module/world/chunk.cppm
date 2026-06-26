module;

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>

#include <includes.hpp>
#include <mutex>
#include <shared_mutex>
#include <algorithm>

export module game.world.chunk;

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
    struct MeshData {
        List<Pos3D<real>> vertices;
        List<Pos3D<real>> normals;
        List<int32> indices;
        List<Pos2D<real>> uvs;
        List<Pos2D<real>> uvs_layer;
        List<Pos3D<real>> collision_faces;
    };

    struct FaceMask {
        int layer = -1;
        bool back_face = false;

        bool operator==(const FaceMask& other) const;
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

        Pos3D<int> chunk_pos;
        TrapezoidHeight height_provider{ VerticalAnchor::absolute(18), VerticalAnchor::absolute(38), 8 };
        std::atomic<bool> generated = false;
        std::atomic<bool> dirty = true;
        std::atomic<bool> collision_built = false;
        mutable std::shared_mutex data_mutex;

        MeshInstance3D* mesh_instance = nullptr;
        Ptr<MeshData> pending_mesh_data = nullptr;
        mutable std::mutex mesh_mutex;

        uint8 chunk_version = 0;

        ~Chunk();
        none clear();

        static uint32 column_seed(int32 seed, int32 x, int32 z);
        static float32 smoothstep(float32 value);
        static Biome lerp_biome(const Biome& a, const Biome& b, float32 t);
        static Biome select_biome_at(int32 wx, int32 wz, Ref<FastNoiseLite> noise, size biome_count);
        static Biome get_blended_biome(int32 wx, int32 wz, Ref<FastNoiseLite> noise, size biome_count);

        none set_block(const Pos3D<uint8>& pos, const Str& block);
        none set_block(const Pos3D<uint8>& pos, uint32 block_id);

        none tag_block(const Pos3D<uint8>& pos, const Str& tag, size tag_data = 0);
        none tag_block(const Pos3D<uint8>& pos, uint32 tag_id, size tag_data = 0);

        bool has_tag(const Pos3D<uint8>& pos, const Str& tag, size tag_data = 0) const;
        bool has_tag(const Pos3D<uint8>& pos, uint32 tag_id, size tag_data = 0) const;

        uint32 get_block(const Pos3D<uint8>& pos) const;
        std::pair<uint32, uint64> get_tag(const Pos3D<uint8>& pos) const;

        none generate_terrain(int32 seed, Ref<FastNoiseLite> noise);
        none generate_mesh(Ptr<Chunk> neighbors[4]);
    };
}
