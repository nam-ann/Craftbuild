module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
ENABLE_WARNING

module game.world.chunk;

namespace craftbuild {
    bool FaceMask::operator==(FaceMask const& other) const {
        return layer == other.layer and back_face == other.back_face;
    }

    ChunkRender::~ChunkRender() noexcept { clear(); }

    void ChunkRender::clear() {
        for (auto& mesh_instance : mesh_instances) {
            if (mesh_instance) mesh_instance->queue_free();
        }
        if (collision_body) collision_body->queue_free();
        if (collision_shape) collision_shape->queue_free();
        if (multi_mesh_instance) multi_mesh_instance->queue_free();
        if (dynamic_body) dynamic_body->queue_free();
    }

    void Chunk::clear() {
        memset(block_ids, 0, sizeof(uint32) * 256);
        meta_ids.clear();
        id2block.clear();
        extended_block_id.clear();
    }

    uint32 Chunk::column_seed(int32 seed, int32 x, int32 z) {
        uint32 h = reinterpret_cast<uint32&>(seed);
        h ^= uint32(x) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= uint32(z) + 0x85ebca6bu + (h << 6) + (h >> 2);
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

    Biome Chunk::lerp_biome(Biome const& a, Biome const& b, float32 t) {
        return {
            a.base_noise + (b.base_noise - a.base_noise) * t,
            a.base_height + (b.base_height - a.base_height) * t,
            a.detail_noise + (b.detail_noise - a.detail_noise) * t,
            a.detail_height + (b.detail_height - a.detail_height) * t,
            a.temperature + (b.temperature - a.temperature) * t,
            int32(std::round(float32(a.min_height) + float32(b.min_height - a.min_height) * t))
        };
    }

    Biome Chunk::select_biome_at(int32 wx, int32 wz, Ref<FastNoiseLite> noise, usize biome_count) {
        if (biome_count == 0) return { 0.01f, 40.0f, 0.4f, 4.0f, 60.0f, 0 };

        float32 const biome_noise_val = noise->get_noise_2d(
            real(wx + 10000) * 0.005f,
            real(wz + 10000) * 0.005f
        );
        float32 const normalized = (biome_noise_val + 1.0f) * 0.5f;
        usize const biome_idx = std::clamp(usize(normalized * biome_count), usize(0), biome_count - 1);
        return BiomeRegistry::get_biome(biome_idx);
    }

    Biome Chunk::get_blended_biome(int32 wx, int32 wz, Ref<FastNoiseLite> noise, usize biome_count) {
        if (biome_count <= 1) return select_biome_at(wx, wz, noise, biome_count);

        static constexpr int32 BLEND_CELL_SIZE = 96;
        float32 const cell_xf = float32(wx) / float32(BLEND_CELL_SIZE);
        float32 const cell_zf = float32(wz) / float32(BLEND_CELL_SIZE);
        int32 const cell_x = int32(std::floor(cell_xf));
        int32 const cell_z = int32(std::floor(cell_zf));
        float32 const tx = smoothstep(cell_xf - float32(cell_x));
        float32 const tz = smoothstep(cell_zf - float32(cell_z));

        int32 const x0 = cell_x * BLEND_CELL_SIZE;
        int32 const z0 = cell_z * BLEND_CELL_SIZE;
        int32 const x1 = x0 + BLEND_CELL_SIZE;
        int32 const z1 = z0 + BLEND_CELL_SIZE;

        Biome const b00 = select_biome_at(x0, z0, noise, biome_count);
        Biome const b10 = select_biome_at(x1, z0, noise, biome_count);
        Biome const b01 = select_biome_at(x0, z1, noise, biome_count);
        Biome const b11 = select_biome_at(x1, z1, noise, biome_count);

        Biome const bx0 = lerp_biome(b00, b10, tx);
        Biome const bx1 = lerp_biome(b01, b11, tx);
        return lerp_biome(bx0, bx1, tz);
    }

    void Chunk::set_block(Pos3D<uint8> const& pos, Str const& block) {
        set_block(pos, BlockRegistry::get_id(block));
    }
    void Chunk::set_block(Pos3D<uint8> const& pos, uint32 block_id) {
        std::unique_lock lock(data_mutex);
        if (block_ids_size >= 255) {
            extended_block_id[pos] = block_id;
            return;
        }

        if (id2block.contains(block_id)) {
            blocks[pos.x][pos.y][pos.z] = id2block[block_id];
            return;
        }

        blocks[pos.x][pos.y][pos.z] = block_ids_size;
        id2block[block_id] = block_ids_size;
        block_ids[block_ids_size++] = block_id;

        auto const& default_metadatas = BlockRegistry::get_block(block_id).value().init_metadatas();
        for (auto const& [meta, data] : default_metadatas) set_block_metadata(pos, meta, data);

        auto const& default_tags = BlockRegistry::get_block(block_id).value().init_tags();
        for (auto tag : default_tags) tag_block(pos, tag);
    }

    void Chunk::tag_block(Pos3D<uint8> const& pos, Str const& tag) {
		return tag_block(pos, TagRegistry::get_id(tag));
    }
    void Chunk::tag_block(Pos3D<uint8> const& pos, uint32 tag_id) {
        std::unique_lock lock(data_mutex);
        tag_ids[pos].emplace(tag_id);
    }

    void Chunk::set_block_metadata(Pos3D<uint8> const& pos, Str const& meta, Str const& meta_data) {
        return set_block_metadata(pos, MetaRegistry::get_id(meta), meta_data);
    }
    void Chunk::set_block_metadata(Pos3D<uint8> const& pos, uint32 meta_id, Str const& meta_data) {
        std::unique_lock lock(data_mutex);
        meta_ids[pos][meta_id] = meta_data;
    }

    bool Chunk::has_tag(Pos3D<uint8> const& pos, Str const& tag) const {
		return has_tag(pos, TagRegistry::get_id(tag));
    }
    bool Chunk::has_tag(Pos3D<uint8> const& pos, uint32 tag_id) const {
        std::shared_lock lock(data_mutex);
        if (pos.x >= WIDTH or pos.y >= HEIGHT or pos.z >= WIDTH) return false;

        return tag_ids.contains(pos) and tag_ids.at(pos).contains(tag_id);
    }

    bool Chunk::has_metadata(Pos3D<uint8> const& pos, Str const& meta, Str const& meta_data) const {
        return has_metadata(pos, MetaRegistry::get_id(meta), meta_data);
    }
    bool Chunk::has_metadata(Pos3D<uint8> const& pos, uint32 tag_id, Str const& tag_data) const {
        std::shared_lock lock(data_mutex);
        if (pos.x >= WIDTH or pos.y >= HEIGHT or pos.z >= WIDTH) return false;

        return meta_ids.contains(pos) and meta_ids.at(pos).contains(tag_id);
    }

    uint32 Chunk::get_block(Pos3D<uint8> const& pos) const {
        if (pos.x >= WIDTH or pos.y >= HEIGHT or pos.z >= WIDTH) return 0;
        if (extended_block_id.contains(pos)) return extended_block_id.at(pos);
        return block_ids[blocks[pos.x][pos.y][pos.z]];
    }

    Set<uint32> const* Chunk::get_tag(Pos3D<uint8> const& pos) const {
        if (pos.x >= WIDTH or pos.y >= HEIGHT or pos.z >= WIDTH) return nullptr;
        return tag_ids.contains(pos) ? &tag_ids.at(pos) : nullptr;
    }

    Dict<uint32, Str> const* Chunk::get_metadata(Pos3D<uint8> const& pos) const {
        if (pos.x >= WIDTH or pos.y >= HEIGHT or pos.z >= WIDTH) return nullptr;
        return meta_ids.contains(pos) ? &meta_ids.at(pos) : nullptr;
    }

    void Chunk::generate_terrain(int32 seed, Ref<FastNoiseLite> noise) {
        auto const AIR     = BlockRegistry::get_id("Air");
        auto const DIRT    = BlockRegistry::get_id("Dirt");
        auto const WATER   = BlockRegistry::get_id("Air");
        auto const GRASS   = BlockRegistry::get_id("Grass Block");
        auto const STONE   = BlockRegistry::get_id("Stone");
        auto const BEDROCK = BlockRegistry::get_id("Bedrock");
        auto const CHEESE_CAVE = CaveRegistry::get_cave(CaveRegistry::get_id("Large Cavern"));

        uint32 new_block_ids[256] = {};
        uint8 new_block_ids_size = 0;

        Dict<uint32, uint8> new_id2block;
        Dict<Pos3D<uint8>, uint32> new_extended_block_id;
        uint8 new_blocks[WIDTH][HEIGHT][WIDTH] = {};

        auto iadd_block = [&](Pos3D<uint8> const& pos, uint32 block_id) {
            if (new_block_ids_size >= 255) {
                new_extended_block_id[pos] = block_id;
                return;
            }

            if (new_id2block.contains(block_id)) {
                new_blocks[pos.x][pos.y][pos.z] = new_id2block[block_id];
                return;
            }

            new_blocks[pos.x][pos.y][pos.z] = new_block_ids_size;
            new_id2block[block_id] = new_block_ids_size;
            new_block_ids[new_block_ids_size++] = block_id;

            auto const& default_metadatas = BlockRegistry::get_block(block_id).value().init_metadatas();
            for (auto const& [meta, data] : default_metadatas) set_block_metadata(pos, meta, data);

            auto const& default_tags = BlockRegistry::get_block(block_id).value().init_tags();
            for (auto tag : default_tags) tag_block(pos, tag);
        };

        usize const biome_count = BiomeRegistry::registry.size();
        for (auto x : range<uint8>(WIDTH)) {
            for (auto z : range<uint8>(WIDTH)) {
                int32 global_x = chunk_pos.x * WIDTH + x;
                int32 global_z = chunk_pos.y * WIDTH + z;

                Biome const current_biome = get_blended_biome(global_x, global_z, noise, biome_count);

                float32 base_noise = noise->get_noise_2d(real(global_x) * current_biome.base_noise, real(global_z) * current_biome.base_noise);
                float32 base_elevation = ((base_noise + 1.0f) * 0.5f) * current_biome.base_height;
                float32 detail_elevation = 0.0f;
                if (current_biome.detail_noise > 0.0f and current_biome.detail_height > 0.0f) {
                    const float32 detail_noise = noise->get_noise_2d(real(global_x) * current_biome.detail_noise, real(global_z) * current_biome.detail_noise);
                    detail_elevation = detail_noise * current_biome.detail_height;
                }
                float32 terrain_base_y = current_biome.min_height + base_elevation + detail_elevation;

                int32 solid_depth = -1;

                for (auto y : range<int16>(HEIGHT - 1, -1)) {
                    if (y == 0) {
                        iadd_block({ x, uint8(y), z }, BEDROCK);
                        continue;
                    }

                    float32 cave_noise = noise->get_noise_3d(
                        global_x * CHEESE_CAVE.frequency,
                        y * CHEESE_CAVE.frequency,
                        global_z * CHEESE_CAVE.frequency
                    );

                    float32 noise_3d = noise->get_noise_3d(
                        real(global_x) * 0.2f,
                        real(y) * 0.3f,
                        real(global_z) * 0.2f
                    );

                    float32 density = terrain_base_y - float32(y) + (noise_3d * 25.0f);
                    auto block_id = AIR;

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

                    iadd_block({ x, uint8(y), z }, block_id);
                }
            }
        }

        {
            std::unique_lock lock(data_mutex);
            std::memcpy(blocks, new_blocks, sizeof(uint8) * Chunk::WIDTH * Chunk::HEIGHT * Chunk::WIDTH);
            std::memcpy(block_ids, new_block_ids, sizeof(uint32) * 256);

            block_ids_size = new_block_ids_size;
            id2block.swap(new_id2block);
            extended_block_id.swap(new_extended_block_id);
        }

        dirty.store(true, std::memory_order_release);
        generated.store(true, std::memory_order_release);
        ++chunk_version;
    }

    static uint8 get_submesh_index(uint8 y) { return std::clamp(y / (Chunk::HEIGHT / 4), 0, 3); }

    void Chunk::generate_mesh(ChunkRender& mesh, Ptr<Chunk> neighbors[4]) {
        Ptr<MeshesData> chunk_data_ptr = new Obj<MeshesData>();
        auto& chunk_data = chunk_data_ptr.value();

        for (auto i : range<uint8>(4)) {
            auto& data = chunk_data[i];

            data.vertices.expect(1024);
            data.normals.expect(1024);
            data.indices.expect(1536);
            data.uvs.expect(1024);
            data.uvs_layer.expect(1024);
            data.collision_faces.expect(1536);
        }

        uint32 const AIR = BlockRegistry::get_id("Air");
        uint32 const TRANSPARENT = TagRegistry::get_id("transparent");

        List<std::shared_mutex*> mutexes_to_lock;
        mutexes_to_lock.append(&data_mutex);
        for (auto i : range<int32>(4)) if (neighbors[i]) mutexes_to_lock.append(&neighbors[i].value().data_mutex);

        std::ranges::sort(mutexes_to_lock);

        auto result = std::ranges::unique(mutexes_to_lock);
        mutexes_to_lock.resize(result.begin() - mutexes_to_lock.begin());

        std::vector<std::shared_lock<std::shared_mutex>> locks;
        for (auto* m : mutexes_to_lock) locks.emplace_back(std::shared_lock<std::shared_mutex>(*m));

        auto is_complex_block = [this, AIR](uint32 id) -> bool {
            auto& block = BlockRegistry::get_block(id);
            if (not block) return false;
            return block.value().get_texture_layer(Face::TOP) == -1;
        };

        auto transparent_or_air = [this, &neighbors, AIR, TRANSPARENT, &is_complex_block](int32 bx, int32 by, int32 bz) -> bool {
            if (by < 0 or by >= Chunk::HEIGHT) return true;

            if (bx < Chunk::WIDTH and bx >= 0 and bz < Chunk::WIDTH and bz >= 0) {
                auto id = get_block({ uint8(bx), uint8(by), uint8(bz) });
                if (id == AIR or is_complex_block(id)) return true;

                auto tag_ptr = get_tag({ uint8(bx), uint8(by), uint8(bz) });
                return tag_ptr and tag_ptr->contains(TRANSPARENT);
            }

            uint8 nid = 0;
            if (bx >= Chunk::WIDTH)       nid = 0;
            else if (bx < 0)              nid = 1;
            else if (bz >= Chunk::WIDTH)  nid = 2;
            else if (bz < 0)              nid = 3;

            auto& neighbor_ptr = neighbors[nid];
			if (not neighbor_ptr) return true;

			Chunk& neighbor = neighbor_ptr.value();
            if (not neighbor.generated.load(std::memory_order_acquire)) return true;

            uint8 lx = uint8((bx % Chunk::WIDTH + Chunk::WIDTH) % Chunk::WIDTH);
            uint8 lz = uint8((bz % Chunk::WIDTH + Chunk::WIDTH) % Chunk::WIDTH);

            auto id = neighbor.get_block({ lx, uint8(by), lz });
            if (id == AIR or is_complex_block(id)) return true;

			auto tag_ptr = neighbor.get_tag({ lx, uint8(by), lz });
            return tag_ptr and tag_ptr->contains(TRANSPARENT);
        };

        auto get_block_layer = [this, AIR, &is_complex_block](int32 bx, int32 by, int32 bz, Face face) -> int32 {
            uint32 id = get_block({ uint8(bx), uint8(by), uint8(bz) });
            if (id == AIR or is_complex_block(id)) return -1;
            auto& block = BlockRegistry::get_block(id);
            if (not block) return -1;
            return block.value().get_texture_layer(face);
        };

        const int64 dims[3] = { Chunk::WIDTH, Chunk::HEIGHT, Chunk::WIDTH };
        const Face front_faces[3] = { Face::RIGHT, Face::TOP,    Face::FRONT };
        const Face back_faces[3] = { Face::LEFT,  Face::BOTTOM, Face::BACK };

        List<FaceMask> mask;
        mask.resize(Chunk::HEIGHT * std::max(Chunk::WIDTH, Chunk::WIDTH));

        uint64 vertex_offsets[4] = {};
        for (auto d : range<int32>(3)) {
            int32 const u = (d + 1) % 3;
            int32 const v = (d + 2) % 3;

            int32 x[3] = {};
            int32 q[3] = {};
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
                            int32 layer = get_block_layer(x[0], x[1], x[2], front_faces[d]);
                            if (layer >= 0) mask[x[u] + x[v] * dims[u]] = { layer, false };
                        }
                        else if (b_inside and a_trans and (not b_trans)) {
                            int32 layer = get_block_layer(x[0] + q[0], x[1] + q[1], x[2] + q[2], back_faces[d]);
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

                        int32 width = 1;
                        while (i + width < dims[u] and mask[(i + width) + j * dims[u]] == current_face) width++;

                        int32 height = 1;
                        bool can_grow = true;
                        while (j + height < dims[v]) {
                            for (int32 k : range(width)) {
                                if (not (mask[(i + k) + (j + height) * dims[u]] == current_face)) {
                                    can_grow = false;
                                    break;
                                }
                            }
                            if (not can_grow) break;
                            ++height;
                        }

                        float32 du[3] = {}; du[u] = float32(width);
                        float32 dv[3] = {}; dv[v] = float32(height);

                        float32 start[3] = {};
                        start[d] = float32(x[d] + 1);
                        start[u] = float32(i);
                        start[v] = float32(j);

                        uint8 avg_y = uint8(start[1]);
                        uint8 s_idx = get_submesh_index(avg_y);

                        auto& data = chunk_data[s_idx];
                        uint64& vertex_offset = vertex_offsets[s_idx];

                        Pos3D p0(start[0], start[1], start[2]);
                        Pos3D p1(start[0] + du[0], start[1] + du[1], start[2] + du[2]);
                        Pos3D p2(start[0] + du[0] + dv[0], start[1] + du[1] + dv[1], start[2] + du[2] + dv[2]);
                        Pos3D p3(start[0] + dv[0], start[1] + dv[1], start[2] + dv[2]);

                        auto get_uv = [&p0, &current_face, width, height, d](Pos3D<float32> const& p) -> Pos2D<real> {
                            float32 const dx = p.x - p0.x;
                            float32 const dy = p.y - p0.y;
                            float32 const dz = p.z - p0.z;

                            if (d == 0)      return Pos2D(current_face.back_face ? height - dz : dz, width - dy);
                            else if (d == 1) return Pos2D(dx, current_face.back_face ? height - dz : dz);
                            else             return Pos2D(current_face.back_face ? width - dx : dx, height - dy);
                        };

                        auto& vertices        = data.vertices;
                        auto& normals         = data.normals;
                        auto& indices         = data.indices;
                        auto& uvs             = data.uvs;
                        auto& uvs_layer       = data.uvs_layer;
                        auto& collision_faces = data.collision_faces;

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

                        Pos3D<real> normal(0, 0, 0);
                        if (d == 0)      normal.x = current_face.back_face ? -1.0f : 1.0f;
                        else if (d == 1) normal.y = current_face.back_face ? -1.0f : 1.0f;
                        else if (d == 2) normal.z = current_face.back_face ? -1.0f : 1.0f;

                        for (auto n : range<int32>(4)) normals.append(normal);

                        Pos2D layer_uv(float32(current_face.layer), 0.0f);
                        for (auto n : range<int32>(4)) uvs_layer.append(layer_uv);

                        indices.append(int32(vertex_offset)); indices.append(int32(vertex_offset + 2)); indices.append(int32(vertex_offset + 1));
                        indices.append(int32(vertex_offset)); indices.append(int32(vertex_offset + 3)); indices.append(int32(vertex_offset + 2));

                        collision_faces.append(vertices[vertex_offset    ]);
                        collision_faces.append(vertices[vertex_offset + 2]);
                        collision_faces.append(vertices[vertex_offset + 1]);
                        collision_faces.append(vertices[vertex_offset + 0]);
                        collision_faces.append(vertices[vertex_offset + 3]);
                        collision_faces.append(vertices[vertex_offset + 2]);

                        vertex_offset += 4;

                        for (auto v_idx : range<int32>(height))
                            for (auto u_idx : range<int32>(width))
                                mask[(i + u_idx) + (j + v_idx) * dims[u]] = { -1, false };

                        i += width;
                    }
                }
            }
        }

        for (uint8 x : range(Chunk::WIDTH)) {
            for (uint16 y : range(Chunk::HEIGHT)) {
                for (uint8 z : range(Chunk::WIDTH)) {
                    uint32 const id = get_block({ x, uint8(y), z });

                    if (not is_complex_block(id)) continue;
                    chunk_data[get_submesh_index(y)].complex_instance.append({ id, {x, uint8(y), z} });
                }
            }
        }

        {
            std::unique_lock lock(mesh.mesh_mutex);
            mesh.pending_meshes_data.swap(chunk_data_ptr);
        }

        dirty.store(false, std::memory_order_release);
    }
}