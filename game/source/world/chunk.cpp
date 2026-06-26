module;

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>

#include <includes.hpp>
#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <cmath>

module game.world.chunk;

namespace craftbuild {
    bool FaceMask::operator==(const FaceMask& other) const {
        return layer == other.layer and back_face == other.back_face;
    }

    Chunk::~Chunk() {
        std::lock_guard lock(mesh_mutex);
        if (pending_mesh_data) {
            pending_mesh_data.clear();
        }
    }

    none Chunk::clear() {
        memset(block_ids, 0, sizeof(uint32) * 256);
        memset(tag_ids, 0, sizeof(uint32) * 256);
        complex_blocks.clear();
    }

    uint32 Chunk::column_seed(int32 seed, int32 x, int32 z) {
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

    float32 Chunk::smoothstep(float32 value) {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }

    Biome Chunk::lerp_biome(const Biome& a, const Biome& b, float32 t) {
        return {
            a.base_noise + (b.base_noise - a.base_noise) * t,
            a.base_height + (b.base_height - a.base_height) * t,
            a.detail_noise + (b.detail_noise - a.detail_noise) * t,
            a.detail_height + (b.detail_height - a.detail_height) * t,
            a.temperature + (b.temperature - a.temperature) * t,
            static_cast<int32>(std::round(static_cast<float32>(a.min_height) + static_cast<float32>(b.min_height - a.min_height) * t))
        };
    }

    Biome Chunk::select_biome_at(int32 wx, int32 wz, Ref<FastNoiseLite> noise, usize biome_count) {
        if (biome_count == 0) return { 0.01f, 40.0f, 0.4f, 4.0f, 60.0f, 0 };

        const float32 biome_noise_val = noise->get_noise_2d(
            static_cast<real_t>(wx + 10000) * 0.005f,
            static_cast<real_t>(wz + 10000) * 0.005f
        );
        const float32 normalized = (biome_noise_val + 1.0f) * 0.5f;
        const usize biome_idx = std::clamp(static_cast<usize>(normalized * biome_count), static_cast<usize>(0), biome_count - 1);
        return BiomeRegistry::get_biome(biome_idx);
    }

    Biome Chunk::get_blended_biome(int32 wx, int32 wz, Ref<FastNoiseLite> noise, usize biome_count) {
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

    none Chunk::set_block(const Pos3D<uint8>& pos, const Str& block) {
        set_block(pos, BlockRegistry::get_id(block));
    }
    none Chunk::set_block(const Pos3D<uint8>& pos, uint32 block_id) {
        std::unique_lock lock(data_mutex);
        if (block_ids_size >= 255) {
            complex_blocks.emplace(pos, BlockStorageFull(block_id, 0, 0));
            return;
        }

        if (id2block.contains(block_id)) {
            blocks[pos.x][pos.y][pos.z].block_id = id2block[block_id];
            return;
        }

        blocks[pos.x][pos.y][pos.z].block_id = block_ids_size;
        id2block[block_id] = block_ids_size;
        block_ids[block_ids_size++] = block_id;

        const auto& default_tags = BlockRegistry::get_block(block_id).value().init_tags();
        for (const auto& tag : default_tags) tag_block(pos, TagRegistry::get_id(tag.first), tag.second);
    }

    none Chunk::tag_block(const Pos3D<uint8>& pos, const Str& tag, usize tag_data) {
        tag_block(pos, TagRegistry::get_id(tag), tag_data);
    }
    none Chunk::tag_block(const Pos3D<uint8>& pos, uint32 tag_id, usize tag_data) {
        std::unique_lock lock(data_mutex);
        if (tag_ids_size >= 256) {
            complex_blocks[pos].tag = tag_id;
            complex_blocks[pos].tag_data = tag_data;
            return;
        }

        const auto tag_pair = std::make_pair(tag_id, tag_data);
        if (id2tag.contains(tag_pair)) {
            blocks[pos.x][pos.y][pos.z].tag = id2tag[tag_pair];
            return;
        }

        blocks[pos.x][pos.y][pos.z].tag = tag_ids_size;
        id2tag[tag_pair] = tag_ids_size;
        tag_ids[tag_ids_size++] = tag_pair;
    }

    bool Chunk::has_tag(const Pos3D<uint8>& pos, const Str& tag, usize tag_data) const {
        return has_tag(pos, TagRegistry::get_id(tag), tag_data);
    }
    bool Chunk::has_tag(const Pos3D<uint8>& pos, uint32 tag_id, usize tag_data) const {
        std::shared_lock lock(data_mutex);
        if (pos.x >= SIZE_X or pos.y >= SIZE_Y or pos.z >= SIZE_Z) return false;

        if (tag_ids_size >= 255) {
            return complex_blocks.at(pos).tag == tag_id and complex_blocks.at(pos).tag_data == tag_data;
        }

        auto& block_tag = blocks[pos.x][pos.y][pos.z].tag;
        for (const auto& tag : tag_ids) if (tag == std::make_pair(tag_id, tag_data)) return true;
        return false;
    }

    uint32 Chunk::get_block(const Pos3D<uint8>& pos) const {
        if (pos.x >= SIZE_X or pos.y >= SIZE_Y or pos.z >= SIZE_Z) return 0;
        if (complex_blocks.contains(pos)) return complex_blocks.at(pos).block_id;
        return block_ids[blocks[pos.x][pos.y][pos.z].block_id];
    }

    std::pair<uint32, uint64> Chunk::get_tag(const Pos3D<uint8>& pos) const {
        if (pos.x >= SIZE_X or pos.y >= SIZE_Y or pos.z >= SIZE_Z) return std::make_pair(0, 0);
        if (complex_blocks.contains(pos)) {
            const auto& block = complex_blocks.at(pos);
            return std::make_pair(block.tag, block.tag_data);
        }
        return tag_ids[blocks[pos.x][pos.y][pos.z].tag];
    }

    none Chunk::generate_terrain(int32 seed, Ref<FastNoiseLite> noise) {
        const uint32 AIR     = BlockRegistry::get_id("Air");
        const uint32 DIRT    = BlockRegistry::get_id("Dirt");
        const uint32 WATER   = BlockRegistry::get_id("Air");
        const uint32 GRASS   = BlockRegistry::get_id("Grass Block");
        const uint32 STONE   = BlockRegistry::get_id("Stone");
        const uint32 BEDROCK = BlockRegistry::get_id("Bedrock");
        const Cave CHEESE_CAVE = CaveRegistry::get_cave(CaveRegistry::get_id("Large Cavern"));

        uint32 new_block_ids[256];
        uint8 new_block_ids_size = 0;

        Dict<uint32, uint8> new_id2block;
        Dict<Pos3D<uint8>, BlockStorageFull> new_complex_blocks;
        auto new_blocks = std::make_unique<BlockStorage[][SIZE_Y][SIZE_Z]>(SIZE_X);

        auto add_block_unlocked = [&](const Pos3D<uint8>& pos, uint32 block_id) {
            if (new_block_ids_size >= 256) {
                new_complex_blocks.emplace(pos, BlockStorageFull(block_id, 0, 0));
                return;
            }

            auto it = new_id2block.find(block_id);
            if (it != new_id2block.end()) {
                new_blocks[pos.x][pos.y][pos.z].block_id = it->second;
                return;
            }

            new_blocks[pos.x][pos.y][pos.z].block_id = new_block_ids_size;
            new_id2block.emplace(block_id, new_block_ids_size);
            new_block_ids[new_block_ids_size++] = block_id;
            };

        const usize biome_count = BiomeRegistry::registry.size();
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
                            if (y <= 62) block_id = WATER;
                            solid_depth = -1;
                        }
                    }

                    add_block_unlocked({ x, (uint8)y, z }, block_id);
                }
            }
        }

        {
            std::unique_lock lock(data_mutex);
            memcpy(blocks, new_blocks.get(), sizeof(BlockStorage) * Chunk::SIZE_X * Chunk::SIZE_Y * Chunk::SIZE_Z);
            memcpy(block_ids, new_block_ids, sizeof(uint32) * 256);

            block_ids_size = new_block_ids_size;
            id2block = std::move(new_id2block);
            complex_blocks = std::move(new_complex_blocks);
        }

        dirty.store(true, std::memory_order_release);
        generated.store(true, std::memory_order_release);
        ++chunk_version;
    }

    none Chunk::generate_mesh(Ptr<Chunk> neighbors[4]) {
        Ptr<MeshData> data_ptr = new MeshData();
        auto& data = data_ptr.value();

        auto& vertices = data.vertices;
        auto& normals = data.normals;
        auto& indices = data.indices;
        auto& uvs = data.uvs;
        auto& uvs_layer = data.uvs_layer;
        auto& collision_faces = data.collision_faces;

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

        Chunk* raw_neighbors[4] = {
            neighbors[0].c_ptr(),
            neighbors[1].c_ptr(),
            neighbors[2].c_ptr(),
            neighbors[3].c_ptr()
        };

        auto transparent_or_air = [this, raw_neighbors, AIR, TRANSPARENT](int bx, int by, int bz) -> bool {
            if (by < 0 or by >= Chunk::SIZE_Y) return true;

            uint32 id = AIR;
            std::pair<uint32, usize> tag = { TRANSPARENT, true };

            if (bx >= Chunk::SIZE_X or bx < 0 or bz >= Chunk::SIZE_Z or bz < 0) {
                uint8 nid = 0;
                if (bx >= Chunk::SIZE_X)      nid = 0;
                else if (bx < 0)              nid = 1;
                else if (bz >= Chunk::SIZE_Z) nid = 2;
                else if (bz < 0)              nid = 3;

                Chunk* neighbor = raw_neighbors[nid];
                if (not neighbor or not neighbor->generated.load(std::memory_order_acquire)) return true;

                uint8 lx = (uint8)((bx % Chunk::SIZE_X + Chunk::SIZE_X) % Chunk::SIZE_X);
                uint8 lz = (uint8)((bz % Chunk::SIZE_Z + Chunk::SIZE_Z) % Chunk::SIZE_Z);

                id = neighbor->get_block({ lx, (uint8)by, lz });
                tag = neighbor->get_tag({ lx, (uint8)by, lz });
            }
            else {
                id = get_block({ (uint8)bx, (uint8)by, (uint8)bz });
                tag = get_tag({ (uint8)bx, (uint8)by, (uint8)bz });
            }

            return id == AIR or (tag.first == TRANSPARENT and TagRegistry::get_value(TRANSPARENT, tag.second));
            };

        auto get_block_layer = [this, AIR](int bx, int by, int bz, Face face) -> int {
            uint32 id = get_block({ (uint8)bx, (uint8)by, (uint8)bz });
            if (id == AIR) return -1;
            auto& block = BlockRegistry::get_block(id);
            if (not block) return -1;
            return block.value().get_texture_layer(face);
            };

        const int64 dims[3] = { Chunk::SIZE_X, Chunk::SIZE_Y, Chunk::SIZE_Z };
        const Face front_faces[3] = { Face::RIGHT, Face::TOP,    Face::FRONT };
        const Face back_faces[3] = { Face::LEFT,  Face::BOTTOM, Face::BACK };

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
                mask.fill({ -1, false });

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
                            if (layer >= 0) mask[x[u] + x[v] * dims[u]] = { layer, true };
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
                            else             return Vector2(current_face.back_face ? width - dx : dx, height - dy);
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
                        if (d == 0) normal.x = current_face.back_face ? -1.0f : 1.0f;
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
            pending_mesh_data = data_ptr;
        }

        dirty.store(false, std::memory_order_release);
    }
}