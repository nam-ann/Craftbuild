module;

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>

#include <includes.hpp>
#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <memory>
#include <cmath>

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

        bool operator==(const FaceMask& other) const {
            return layer == other.layer and back_face == other.back_face;
        }
    };

    class Chunk {
    public:
        inline static constexpr uint8 SIZE_X = 16;
        inline static constexpr uint8 SIZE_Y = 255;
        inline static constexpr uint8 SIZE_Z = 16;

        Dict<uint8, uint32> block_ids;
        Dict<uint8, std::pair<uint32, size>> tag_ids;
        Dict<Pos3D<uint8>, BlockStorageFull> complex_blocks;
        BlockStorage blocks[SIZE_X][SIZE_Y][SIZE_Z] = {};

        MeshInstance3D* mesh_instance = nullptr;
        Vector3i chunk_pos;
        TrapezoidHeight height_provider{ VerticalAnchor::absolute(18), VerticalAnchor::absolute(38), 8 };

        std::atomic<bool> generated = false;
        std::atomic<bool> dirty = true;
        std::atomic<bool> collision_built = false;

        std::atomic<bool> mesh_ready{ false };
        Ptr<MeshData> pending_mesh_data = nullptr;
        mutable std::mutex mesh_mutex;
        mutable std::shared_mutex data_mutex;

        ~Chunk() {
            std::lock_guard lock(mesh_mutex);
            if (pending_mesh_data) {
                pending_mesh_data.clear();
            }
        }

        static uint32 column_seed(int32 seed, int32 x, int32 z) {
            uint32 h = static_cast<uint32>(seed);
            h ^= static_cast<uint32>(x) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= static_cast<uint32>(z) + 0x85ebca6bu + (h << 6) + (h >> 2);
            h ^= h >> 16;
            h *= 0x7feb352du;
            h ^= h >> 15;
            h *= 0x846ca68bu;
            h ^= h >> 16;
            return h;
        }

        static float32 smoothstep(float32 value) {
            value = std::clamp(value, 0.0f, 1.0f);
            return value * value * (3.0f - 2.0f * value);
        }

        static Biome lerp_biome(const Biome& a, const Biome& b, float32 t) {
            return {
                a.base_noise    + (b.base_noise - a.base_noise)       * t,
                a.base_height   + (b.base_height - a.base_height)     * t,
                a.detail_noise  + (b.detail_noise - a.detail_noise)   * t,
                a.detail_height + (b.detail_height - a.detail_height) * t,
                a.temperature   + (b.temperature - a.temperature)     * t,
                static_cast<int32>(std::round(static_cast<float32>(a.min_height) + static_cast<float32>(b.min_height - a.min_height) * t))
            };
        }

        static Biome select_biome_at(int32 wx, int32 wz, Ref<FastNoiseLite> noise, size biome_count) {
            if (biome_count == 0) return { 0.01f, 40.0f, 0.4f, 4.0f, 60.0f, 0 };

            const float32 biome_noise_val = noise->get_noise_2d(
                static_cast<real_t>(wx + 10000) * 0.005f,
                static_cast<real_t>(wz + 10000) * 0.005f
            );
            const float32 normalized = (biome_noise_val + 1.0f) * 0.5f;
            const size biome_idx = std::clamp(static_cast<size>(normalized * biome_count), static_cast<size>(0), biome_count - 1);
            return BiomeRegistry::get_biome(biome_idx);
        }

        static Biome get_blended_biome(int32 wx, int32 wz, Ref<FastNoiseLite> noise, size biome_count) {
            if (biome_count <= 1) return select_biome_at(wx, wz, noise, biome_count);

            static constexpr int32 BLEND_CELL_SIZE = 96;
            const float32 cell_xf = static_cast<float32>(wx) / static_cast<float32>(BLEND_CELL_SIZE);
            const float32 cell_zf = static_cast<float32>(wz) / static_cast<float32>(BLEND_CELL_SIZE);
            const int32 cell_x = static_cast<int32>(std::floor(cell_xf));
            const int32 cell_z = static_cast<int32>(std::floor(cell_zf));
            const float32 tx = smoothstep(cell_xf - static_cast<float32>(cell_x));
            const float32 tz = smoothstep(cell_zf - static_cast<float32>(cell_z));

            const int32 x0 = cell_x * BLEND_CELL_SIZE;
            const int32 z0 = cell_z * BLEND_CELL_SIZE;
            const int32 x1 = x0 + BLEND_CELL_SIZE;
            const int32 z1 = z0 + BLEND_CELL_SIZE;

            const Biome b00 = select_biome_at(x0, z0, noise, biome_count);
            const Biome b10 = select_biome_at(x1, z0, noise, biome_count);
            const Biome b01 = select_biome_at(x0, z1, noise, biome_count);
            const Biome b11 = select_biome_at(x1, z1, noise, biome_count);

            const Biome bx0 = lerp_biome(b00, b10, tx);
            const Biome bx1 = lerp_biome(b01, b11, tx);
            return lerp_biome(bx0, bx1, tz);
        }

        none set_block(const Pos3D<uint8>& pos, const Str& block) {
			set_block(pos, BlockRegistry::get_id(block));
        }
        none set_block(const Pos3D<uint8>& pos, uint32 block_id) {
            std::unique_lock lock(data_mutex);
            if (block_ids.size() >= 256) {
                complex_blocks.emplace(pos, BlockStorageFull(block_id, 0, 0));
                return;
            }

            auto it = std::find_if(block_ids.begin(), block_ids.end(), [block_id](const auto& pair) {
                return pair.second == block_id;
            });
            if (it != block_ids.end()) {
                blocks[pos.x][pos.y][pos.z].block_id = it->first;
                return;
            }

            blocks[pos.x][pos.y][pos.z].block_id = block_ids.size();
            block_ids.emplace(block_ids.size(), block_id);
        }

        none tag_block(const Pos3D<uint8>& pos, const Str& tag, size tag_data = 0) {
            tag_block(pos, TagRegistry::get_id(tag), tag_data);
        }
        none tag_block(const Pos3D<uint8>& pos, uint32 tag_id, size tag_data = 0) {
            std::unique_lock lock(data_mutex);
            if (tag_ids.size() >= 256) {
                complex_blocks[pos].tag = tag_id;
                complex_blocks[pos].tag_data = tag_data;
                return;
            }

            auto it = std::find_if(tag_ids.begin(), tag_ids.end(), [tag_id, tag_data](const auto& pair) {
                return pair.second == std::make_pair(tag_id, tag_data);
            });
            if (it != tag_ids.end()) {
                blocks[pos.x][pos.y][pos.z].tag = it->first;
                return;
            }

            blocks[pos.x][pos.y][pos.z].tag = tag_ids.size();
            tag_ids.emplace(tag_ids.size(), std::make_pair(tag_id, tag_data));
        }

        bool has_tag(const Pos3D<uint8>& pos, const Str& tag, size tag_data = 0) const {
			return has_tag(pos, TagRegistry::get_id(tag), tag_data);
        }
        bool has_tag(const Pos3D<uint8>& pos, uint32 tag_id, size tag_data = 0) const {
            std::shared_lock lock(data_mutex);
            if (pos.x >= SIZE_X or pos.y >= SIZE_Y or pos.z >= SIZE_Z) return false;

            if (tag_ids.size() >= 256) {
                return complex_blocks.at(pos).tag == tag_id and complex_blocks.at(pos).tag_data == tag_data;
            }

            auto& block_tag = blocks[pos.x][pos.y][pos.z].tag;
			return tag_ids.find(block_tag) != tag_ids.end() and tag_ids.at(block_tag) == std::make_pair(tag_id, tag_data);
        }

        template <bool lock = true>
        uint32 get_block(const Pos3D<uint8>& pos) const;
        template <bool lock = true>
        std::pair<uint32, size> get_tag(const Pos3D<uint8>& pos) const;

        template<>
        uint32 get_block<true>(const Pos3D<uint8>& pos) const {
            std::shared_lock lock(data_mutex);
            if (pos.x >= SIZE_X or pos.y >= SIZE_Y or pos.z >= SIZE_Z) return 0;
            auto it = complex_blocks.find(pos);
            if (it != complex_blocks.end()) {
                return it->second.block_id;
            }
            return block_ids.find(blocks[pos.x][pos.y][pos.z].block_id) != block_ids.end() ? block_ids.at(blocks[pos.x][pos.y][pos.z].block_id) : 0;
        }
        template<>
        std::pair<uint32, size> get_tag<true>(const Pos3D<uint8>& pos) const {
            std::shared_lock lock(data_mutex);
            if (pos.x >= SIZE_X or pos.y >= SIZE_Y or pos.z >= SIZE_Z) return std::make_pair(0, 0);
            auto it = complex_blocks.find(pos);
            if (it != complex_blocks.end()) {
                return std::make_pair(it->second.tag, it->second.tag_data);
            }
            return tag_ids.find(blocks[pos.x][pos.y][pos.z].tag) != tag_ids.end() ? tag_ids.at(blocks[pos.x][pos.y][pos.z].tag) : std::make_pair(0U, (size)0);
        }

        template<>
        uint32 get_block<false>(const Pos3D<uint8>& pos) const {
            if (pos.x >= SIZE_X or pos.y >= SIZE_Y or pos.z >= SIZE_Z) return 0;
            auto it = complex_blocks.find(pos);
            if (it != complex_blocks.end()) {
                return it->second.block_id;
            }
            return block_ids.find(blocks[pos.x][pos.y][pos.z].block_id) != block_ids.end() ? block_ids.at(blocks[pos.x][pos.y][pos.z].block_id) : 0;
        }
        template<>
        std::pair<uint32, size> get_tag<false>(const Pos3D<uint8>& pos) const {
            if (pos.x >= SIZE_X or pos.y >= SIZE_Y or pos.z >= SIZE_Z) return std::make_pair(0, 0);
            auto it = complex_blocks.find(pos);
            if (it != complex_blocks.end()) {
                return std::make_pair(it->second.tag, it->second.tag_data);
            }
            return tag_ids.find(blocks[pos.x][pos.y][pos.z].tag) != tag_ids.end() ? tag_ids.at(blocks[pos.x][pos.y][pos.z].tag) : std::make_pair(0U, (size)0);
        }

        none generate_terrain(int32 seed, Ref<FastNoiseLite> noise) {
            const uint32 AIR     = BlockRegistry::get_id("Air");
            const uint32 DIRT    = BlockRegistry::get_id("Dirt");
            const uint32 WATER   = BlockRegistry::get_id("Air");
            const uint32 GRASS   = BlockRegistry::get_id("Grass Block");
            const uint32 STONE   = BlockRegistry::get_id("Stone");
            const uint32 BEDROCK = BlockRegistry::get_id("Bedrock");
            const Cave CHEESE_CAVE = CaveRegistry::get_cave(CaveRegistry::get_id("Large Cavern"));

            auto new_blocks = std::make_unique<BlockStorage[][SIZE_Y][SIZE_Z]>(SIZE_X);
            Dict<uint8, uint32> new_block_ids;
            Dict<uint8, std::pair<uint32, size>> new_tag_ids;
            Dict<Pos3D<uint8>, BlockStorageFull> new_complex_blocks;

            auto add_block_unlocked = [&](const Pos3D<uint8>& pos, uint32 block_id) {
                if (new_block_ids.size() >= 256) {
                    new_complex_blocks.emplace(pos, BlockStorageFull(block_id, 0, 0));
                    return;
                }

                auto it = std::find_if(new_block_ids.begin(), new_block_ids.end(), [block_id](const auto& pair) {
                    return pair.second == block_id;
                });
                if (it != new_block_ids.end()) {
                    new_blocks[pos.x][pos.y][pos.z].block_id = it->first;
                    return;
                }

                const uint8 local_id = static_cast<uint8>(new_block_ids.size());
                new_blocks[pos.x][pos.y][pos.z].block_id = local_id;
                new_block_ids.emplace(local_id, block_id);
            };

            const size biome_count = BiomeRegistry::registry.size();
            for (auto x : range<uint8>(SIZE_X)) {
                for (auto z : range<uint8>(SIZE_Z)) {
                    int32 global_x = chunk_pos.x * SIZE_X + x;
                    int32 global_z = chunk_pos.z * SIZE_Z + z;

                    const Biome current_biome = get_blended_biome(global_x, global_z, noise, biome_count);

                    float32 base_noise = noise->get_noise_2d(static_cast<real_t>(global_x) * current_biome.base_noise, static_cast<real_t>(global_z) * current_biome.base_noise);
                    float32 base_elevation = ((base_noise + 1.0f) * 0.5f) * current_biome.base_height;
                    float32 detail_elevation = 0.0f;
                    if (current_biome.detail_noise > 0.0f and current_biome.detail_height > 0.0f) {
                        const float32 detail_noise = noise->get_noise_2d(static_cast<real_t>(global_x) * current_biome.detail_noise, static_cast<real_t>(global_z) * current_biome.detail_noise);
                        detail_elevation = detail_noise * current_biome.detail_height;
                    }
                    float32 terrain_base_y = current_biome.min_height + base_elevation + detail_elevation;

                    int solid_depth = -1;

                    for (auto y : range<int16>(SIZE_Y - 1, -1)) {
                        if (y == 0) {
                            add_block_unlocked({ x, (uint8)y, z }, BEDROCK);
                            continue;
                        }

                        float32 cave_noise = noise->get_noise_3d(
                            global_x * CHEESE_CAVE.frequency,
                            y * CHEESE_CAVE.frequency,
                            global_z * CHEESE_CAVE.frequency
                        );

                        float32 noise_3d = noise->get_noise_3d(
                            static_cast<real_t>(global_x) * 0.2f,
                            static_cast<real_t>(y) * 0.3f,
                            static_cast<real_t>(global_z) * 0.2f
                        );

                        float32 density = terrain_base_y - static_cast<float32>(y) + (noise_3d * 25.0f);
                        uint32 block_id = AIR;

                        if (cave_noise <= CHEESE_CAVE.threshold) {
                            if (density > current_biome.base_height * 0.005f) {
                                if (solid_depth == -1) {
                                    block_id = GRASS;
                                    solid_depth = 1;
                                }
                                else if (solid_depth < 4) {
                                    block_id = DIRT;
                                    solid_depth++;
                                }
                                else block_id = STONE;
                            }
                            else {
                                if (y <= 62) block_id = AIR;
                                solid_depth = -1;
                            }
                        }

                        add_block_unlocked({ x, (uint8)y, z }, block_id);
                    }
                }
            }

            {
                std::unique_lock lock(data_mutex);
                std::memcpy(blocks, new_blocks.get(), sizeof(blocks));
                block_ids = std::move(new_block_ids);
                tag_ids = std::move(new_tag_ids);
                complex_blocks = std::move(new_complex_blocks);
            }

            generated.store(true, std::memory_order_release);
            dirty.store(true, std::memory_order_release);
        }

        none generate_mesh(Ptr<Chunk> neighbors[4]) {
            Ptr<MeshData> data = new MeshData();
            auto& vertices        = data.value().vertices;
            auto& normals         = data.value().normals;
            auto& indices         = data.value().indices;
            auto& uvs             = data.value().uvs;
            auto& uvs_layer       = data.value().uvs_layer;
            auto& collision_faces = data.value().collision_faces;

            const uint32 AIR = BlockRegistry::get_id("Air");
            const uint32 TRANSPARENT = TagRegistry::get_id("transparent");

            vertices.expect(4096);
            normals.expect(4096);
            uvs.expect(4096);
            uvs_layer.expect(4096);
            indices.expect(6144);
            collision_faces.expect(6144);

            std::vector<std::shared_mutex*> mutexes_to_lock;
            mutexes_to_lock.push_back(&data_mutex);
            for (auto i : range<int>(4)) if (neighbors[i]) mutexes_to_lock.push_back(&neighbors[i].value().data_mutex);

            std::sort(mutexes_to_lock.begin(), mutexes_to_lock.end());
            mutexes_to_lock.erase(std::unique(mutexes_to_lock.begin(), mutexes_to_lock.end()), mutexes_to_lock.end());

            std::vector<std::unique_ptr<std::shared_lock<std::shared_mutex>>> locks;
            for (auto* m : mutexes_to_lock) locks.push_back(std::make_unique<std::shared_lock<std::shared_mutex>>(*m));

            dirty.store(false, std::memory_order_release);

            auto transparent_or_air = [&](int bx, int by, int bz) -> bool {
                if (by < 0 or by >= Chunk::SIZE_Y) return true;

                uint32 id = AIR;
                std::pair<uint32, size> tag = { TRANSPARENT, true };

                if (bx >= Chunk::SIZE_X or bx < 0 or bz >= Chunk::SIZE_Z or bz < 0) {
                    uint8 nid = 0;
                    if (bx >= Chunk::SIZE_X)      nid = 0;
                    else if (bx < 0)              nid = 1;
                    else if (bz >= Chunk::SIZE_Z) nid = 2;
                    else if (bz < 0)              nid = 3;

                    Ptr<Chunk> neighbor = neighbors[nid];
                    if (not neighbor or not neighbor.value().generated.load(std::memory_order_acquire)) return true;

                    uint8 lx = (uint8)((bx % Chunk::SIZE_X + Chunk::SIZE_X) % Chunk::SIZE_X);
                    uint8 lz = (uint8)((bz % Chunk::SIZE_Z + Chunk::SIZE_Z) % Chunk::SIZE_Z);

                    id = neighbor.value().get_block<false>({ lx, (uint8)by, lz });
                    tag = neighbor.value().get_tag<false>({ lx, (uint8)by, lz });
                }
                else {
                    id = get_block<false>({ (uint8)bx, (uint8)by, (uint8)bz });
                    tag = get_tag<false>({ (uint8)bx, (uint8)by, (uint8)bz });
                }

                if (id == AIR) return true;
                if (tag.first != TRANSPARENT) return false;

                return TagRegistry::get_value(TRANSPARENT, tag.second) == true;
            };

            auto get_block_layer = [&](int bx, int by, int bz, Face face) -> int {
                uint32 id = get_block<false>({ (uint8)bx, (uint8)by, (uint8)bz });
                if (id == AIR) return -1;
                Ptr<Block> block = BlockRegistry::get_block(id);
                if (not block) return -1;
                return block.value().get_texture_layer(face);
            };

            const int64 dims[3] = { Chunk::SIZE_X, Chunk::SIZE_Y, Chunk::SIZE_Z };
            const Face front_faces[3] = { Face::RIGHT, Face::TOP,    Face::FRONT };
            const Face back_faces[3] =  { Face::LEFT,  Face::BOTTOM, Face::BACK  };

            List<FaceMask> mask;
            mask.resize(Chunk::SIZE_Y * std::max(Chunk::SIZE_X, Chunk::SIZE_Z));

            uint64 vertex_offset = 0;
            for (auto d : range<int>(3)) {
                const int u = (d + 1) % 3;
                const int v = (d + 2) % 3;

                int64 x[3] = { 0, 0, 0 };
                int64 q[3] = { 0, 0, 0 };
                q[d] = 1;

                for (x[d] = -1; x[d] < dims[d]; ++x[d]) {
                    for (x[v] = 0; x[v] < dims[v]; ++x[v]) {
                        for (x[u] = 0; x[u] < dims[u]; ++x[u]) {
                            const bool a_inside = (x[d] >= 0);
                            const bool b_inside = (x[d] + 1 < dims[d]);

                            const bool a_trans = transparent_or_air(x[0], x[1], x[2]);
                            const bool b_trans = transparent_or_air(x[0] + q[0], x[1] + q[1], x[2] + q[2]);

                            if (a_inside and (not a_trans) and b_trans) {
                                int layer = get_block_layer(x[0], x[1], x[2], front_faces[d]);
                                if (layer >= 0) mask[x[u] + x[v] * dims[u]] = { layer, false };
                            }
                            else if (b_inside and a_trans and (not b_trans)) {
                                int layer = get_block_layer(x[0] + q[0], x[1] + q[1], x[2] + q[2], back_faces[d]);
                                if (layer >= 0) mask[x[u] + x[v] * dims[u]] = {layer, true };
                            }
                        }
                    }

                    for (auto j : range<int64>(dims[v])) {
                        int64 i = 0;
                        while (i < dims[u]) {
                            FaceMask current_face = mask[i + j * dims[u]];
                            if (current_face.layer < 0) {
                                ++i;
                                continue;
                            }

                            int width = 1;
                            while (i + width < dims[u] and mask[(i + width) + j * dims[u]] == current_face) width++;

                            int height = 1;
                            bool can_grow = true;
                            while (j + height < dims[v]) {
                                for (int k = 0; k < width; ++k) {
                                    if (not (mask[(i + k) + (j + height) * dims[u]] == current_face)) {
                                        can_grow = false;
                                        break;
                                    }
                                }
                                if (not can_grow) break;
                                ++height;
                            }

                            float32 du[3] = { 0, 0, 0 }; du[u] = (float32)width;
                            float32 dv[3] = { 0, 0, 0 }; dv[v] = (float32)height;

                            float32 start[3] = { 0, 0, 0 };
                            start[d] = (float32)(x[d] + 1);
                            start[u] = (float32)i;
                            start[v] = (float32)j;

                            Pos3D<float32> p0(start[0], start[1], start[2]);
                            Pos3D<float32> p1(start[0] + du[0], start[1] + du[1], start[2] + du[2]);
                            Pos3D<float32> p2(start[0] + du[0] + dv[0], start[1] + du[1] + dv[1], start[2] + du[2] + dv[2]);
                            Pos3D<float32> p3(start[0] + dv[0], start[1] + dv[1], start[2] + dv[2]);

                            auto get_uv = [&](const Pos3D<float32>& p) -> Vector2 {
                                const float32 dx = p.x - p0.x;
                                const float32 dy = p.y - p0.y;
                                const float32 dz = p.z - p0.z;

                                if (d == 0)      return Vector2(current_face.back_face ? height - dz : dz, width - dy);
                                else if (d == 1) return Vector2(dx, current_face.back_face ? height - dz : dz);
                                else             return Vector2(current_face.back_face ? width - dx : dx, height -dy);
                            };

                            if (not current_face.back_face) {
                                vertices.append(p0); vertices.append(p1);
                                vertices.append(p2); vertices.append(p3);

                                uvs.append(get_uv(p0));
                                uvs.append(get_uv(p1));
                                uvs.append(get_uv(p2));
                                uvs.append(get_uv(p3));
                            }
                            else {
                                vertices.append(p0); vertices.append(p3);
                                vertices.append(p2); vertices.append(p1);

                                uvs.append(get_uv(p0));
                                uvs.append(get_uv(p3));
                                uvs.append(get_uv(p2));
                                uvs.append(get_uv(p1));
                            }

                            Vector3 normal(0, 0, 0);
                            if      (d == 0) normal.x = current_face.back_face ? -1.0f : 1.0f;
                            else if (d == 1) normal.y = current_face.back_face ? -1.0f : 1.0f;
                            else if (d == 2) normal.z = current_face.back_face ? -1.0f : 1.0f;

                            for (auto n : range<int>(4)) normals.append(normal);

                            Vector2 layer_uv(static_cast<float>(current_face.layer), 0.0f);
                            for (auto n : range<int>(4)) uvs_layer.append(layer_uv);

                            indices.append(vertex_offset + 0); indices.append(vertex_offset + 2); indices.append(vertex_offset + 1);
                            indices.append(vertex_offset + 0); indices.append(vertex_offset + 3); indices.append(vertex_offset + 2);

                            collision_faces.append(vertices[vertex_offset + 0]);
                            collision_faces.append(vertices[vertex_offset + 2]);
                            collision_faces.append(vertices[vertex_offset + 1]);
                            collision_faces.append(vertices[vertex_offset + 0]);
                            collision_faces.append(vertices[vertex_offset + 3]);
                            collision_faces.append(vertices[vertex_offset + 2]);

                            vertex_offset += 4;

                            for (auto v_idx : range<int>(height))
                                for (auto u_idx : range<int>(width))
                                    mask[(i + u_idx) + (j + v_idx) * dims[u]] = { -1, false };

                            i += width;
                        }
                    }
                }
            }

            {
                std::lock_guard lock(mesh_mutex);
                pending_mesh_data = data;
            }

            mesh_ready.store(true, std::memory_order_release);
            dirty.store(false, std::memory_order_release);
        }
    };
}
