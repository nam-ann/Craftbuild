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
    constexpr FaceMask::FaceMask() {}
    constexpr FaceMask::FaceMask(i32 layer, bool back_face) : value((layer + 1) | (back_face ? BACK_FACE_BIT : 0)) {}

    i32 FaceMask::layer() const {
        return (value & ~BACK_FACE_BIT) - 1;
    }

    bool FaceMask::back_face() const {
        return (u32(value) & BACK_FACE_BIT) != 0;
    }

    bool FaceMask::operator==(FaceMask const& other) const {
        return value == other.value;
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
        memset(block_ids, 0, sizeof(u32) * 256);
        memset(blocks, 0, sizeof(u8) * WIDTH * HEIGHT * WIDTH);
        block_ids_size = 0;

        tag_ids.clear();
        meta_ids.clear();
        id2block.clear();
        extended_block_id.clear();
    }

    u32 Chunk::column_seed(i32 seed, i32 x, i32 z) {
        u32 h = reinterpret_cast<u32&>(seed);
        h ^= u32(x) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= u32(z) + 0x85ebca6bu + (h << 6) + (h >> 2);
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return h;
    }

    f32 Chunk::smoothstep(f32 value) {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }

    Biome Chunk::lerp_biome(Biome const& a, Biome const& b, f32 t) {
        return {
            a.base_noise + (b.base_noise - a.base_noise) * t,
            a.base_height + (b.base_height - a.base_height) * t,
            a.detail_noise + (b.detail_noise - a.detail_noise) * t,
            a.detail_height + (b.detail_height - a.detail_height) * t,
            a.temperature + (b.temperature - a.temperature) * t,
            i32(std::round(f32(a.min_height) + f32(b.min_height - a.min_height) * t))
        };
    }

    Biome Chunk::select_biome_at(i32 wx, i32 wz, Ref<FastNoiseLite> noise, usize biome_count) {
        if (biome_count == 0) return { 0.01f, 40.0f, 0.4f, 4.0f, 60.0f, 0 };

        f32 const biome_noise_val = noise->get_noise_2d(
            fsize(wx + 10000) * 0.005f,
            fsize(wz + 10000) * 0.005f
        );
        f32 const normalized = (biome_noise_val + 1.0f) * 0.5f;
        usize const biome_idx = std::clamp(usize(normalized * biome_count), usize(0), biome_count - 1);
        return biome_registry::get_biome(biome_idx);
    }

    Biome Chunk::get_blended_biome(i32 wx, i32 wz, Ref<FastNoiseLite> noise, usize biome_count) {
        if (biome_count <= 1) return select_biome_at(wx, wz, noise, biome_count);

        static constexpr i32 BLEND_CELL_SIZE = 96;
        f32 const cell_xf = f32(wx) / f32(BLEND_CELL_SIZE);
        f32 const cell_zf = f32(wz) / f32(BLEND_CELL_SIZE);
        i32 const cell_x = i32(std::floor(cell_xf));
        i32 const cell_z = i32(std::floor(cell_zf));
        f32 const tx = smoothstep(cell_xf - f32(cell_x));
        f32 const tz = smoothstep(cell_zf - f32(cell_z));

        i32 const x0 = cell_x * BLEND_CELL_SIZE;
        i32 const z0 = cell_z * BLEND_CELL_SIZE;
        i32 const x1 = x0 + BLEND_CELL_SIZE;
        i32 const z1 = z0 + BLEND_CELL_SIZE;

        Biome const b00 = select_biome_at(x0, z0, noise, biome_count);
        Biome const b10 = select_biome_at(x1, z0, noise, biome_count);
        Biome const b01 = select_biome_at(x0, z1, noise, biome_count);
        Biome const b11 = select_biome_at(x1, z1, noise, biome_count);

        Biome const bx0 = lerp_biome(b00, b10, tx);
        Biome const bx1 = lerp_biome(b01, b11, tx);
        return lerp_biome(bx0, bx1, tz);
    }

    void Chunk::set_block(Pos3D<u8> const& pos, Str const& block) {
        set_block(pos, block_registry::get_id(block));
    }
    void Chunk::set_block(Pos3D<u8> const& pos, u32 block_id) {
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

        auto const& default_metadatas = block_registry::get_block(block_id).init_metadatas();
        for (auto const& [meta, data] : default_metadatas) set_block_metadata(pos, meta, data);

        auto const& default_tags = block_registry::get_block(block_id).init_tags();
        for (auto tag : default_tags) tag_block(pos, tag);
    }

    void Chunk::tag_block(Pos3D<u8> const& pos, Str const& tag) {
		return tag_block(pos, tag_registry::get_id(tag));
    }
    void Chunk::tag_block(Pos3D<u8> const& pos, u32 tag_id) {
        std::unique_lock lock(data_mutex);
        tag_ids[pos].emplace(tag_id);
    }

    void Chunk::set_block_metadata(Pos3D<u8> const& pos, Str const& meta, Str const& meta_data) {
        return set_block_metadata(pos, meta_registry::get_id(meta), meta_data);
    }
    void Chunk::set_block_metadata(Pos3D<u8> const& pos, u32 meta_id, Str const& meta_data) {
        std::unique_lock lock(data_mutex);
        meta_ids[pos][meta_id] = meta_data;
    }

    bool Chunk::has_tag(Pos3D<u8> const& pos, Str const& tag) const {
		return has_tag(pos, tag_registry::get_id(tag));
    }
    bool Chunk::has_tag(Pos3D<u8> const& pos, u32 tag_id) const {
        std::shared_lock lock(data_mutex);
        if (pos.x >= WIDTH or pos.y >= HEIGHT or pos.z >= WIDTH) return false;

        return tag_ids.contains(pos) and tag_ids.at(pos).contains(tag_id);
    }

    bool Chunk::has_metadata(Pos3D<u8> const& pos, Str const& meta, Str const& meta_data) const {
        return has_metadata(pos, meta_registry::get_id(meta), meta_data);
    }
    bool Chunk::has_metadata(Pos3D<u8> const& pos, u32 tag_id, Str const& tag_data) const {
        std::shared_lock lock(data_mutex);
        if (pos.x >= WIDTH or pos.y >= HEIGHT or pos.z >= WIDTH) return false;

        return meta_ids.contains(pos) and meta_ids.at(pos).contains(tag_id);
    }

    u32 Chunk::get_block(Pos3D<u8> const& pos) const {
        if (pos.x >= WIDTH or pos.y >= HEIGHT or pos.z >= WIDTH) return 0;
        if (extended_block_id.contains(pos)) return extended_block_id.at(pos);
        return block_ids[blocks[pos.x][pos.y][pos.z]];
    }

    Set<u32> const* Chunk::get_tag(Pos3D<u8> const& pos) const {
        if (pos.x >= WIDTH or pos.y >= HEIGHT or pos.z >= WIDTH) return nullptr;
        return tag_ids.contains(pos) ? &tag_ids.at(pos) : nullptr;
    }

    Dict<u32, Str> const* Chunk::get_metadata(Pos3D<u8> const& pos) const {
        if (pos.x >= WIDTH or pos.y >= HEIGHT or pos.z >= WIDTH) return nullptr;
        return meta_ids.contains(pos) ? &meta_ids.at(pos) : nullptr;
    }

    void Chunk::generate_terrain(i32 seed, Ref<FastNoiseLite> noise) {
        auto const AIR     = block_registry::get_id("Air");
        auto const DIRT    = block_registry::get_id("Dirt");
        auto const WATER   = block_registry::get_id("Air");
        auto const GRASS   = block_registry::get_id("Grass Block");
        auto const STONE   = block_registry::get_id("Stone");
        auto const BEDROCK = block_registry::get_id("Bedrock");
        auto const CHEESE_CAVE = cave_registry::get_cave(cave_registry::get_id("Large Cavern"));

        u32 new_block_ids[256] = {};
        u8 new_block_ids_size = 0;

        Dict<u32, u8> new_id2block;
        Dict<Pos3D<u8>, u32> new_extended_block_id;
        u8 new_blocks[WIDTH][HEIGHT][WIDTH] = {};

        Dict<Pos3D<u8>, Dict<u32, Str>> new_meta_ids;
        Dict<Pos3D<u8>, Set<u32>> new_tag_ids;

        auto iadd_block_metadata = [&new_meta_ids](Pos3D<u8> const& pos, u32 meta_id, Str const& meta_data = "") {
            new_meta_ids[pos][meta_id] = meta_data;
        };

        auto itag_block = [&new_tag_ids](Pos3D<u8> const& pos, u32 tag_id) {
            new_tag_ids[pos].emplace(tag_id);
        };

        auto iadd_block = [&](Pos3D<u8> const& pos, u32 block_id) {
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

            auto const& default_metadatas = block_registry::get_block(block_id).init_metadatas();
            for (auto const& [meta, data] : default_metadatas) iadd_block_metadata(pos, meta, data);

            auto const& default_tags = block_registry::get_block(block_id).init_tags();
            for (auto tag : default_tags) itag_block(pos, tag);
        };

        usize const biome_count = len(biome_registry::registry);
        for (auto x : range<u8>(WIDTH)) {
            for (auto z : range<u8>(WIDTH)) {
                i32 const global_x = chunk_pos.x * WIDTH + x;
                i32 const global_z = chunk_pos.y * WIDTH + z;

                Biome const current_biome = get_blended_biome(global_x, global_z, noise, biome_count);

                f32 const base_noise = noise->get_noise_2d(fsize(global_x) * current_biome.base_noise, fsize(global_z) * current_biome.base_noise);
                f32 const base_elevation = ((base_noise + 1.0f) * 0.5f) * current_biome.base_height;
                
                f32 detail_elevation = 0.0f;
                if (current_biome.detail_noise > 0.0f and current_biome.detail_height > 0.0f) {
                    f32 const detail_noise = noise->get_noise_2d(fsize(global_x) * current_biome.detail_noise, fsize(global_z) * current_biome.detail_noise);
                    detail_elevation = detail_noise * current_biome.detail_height;
                }

                f32 const terrain_base_y = current_biome.min_height + base_elevation + detail_elevation;
                i32 solid_depth = -1;

                for (auto y : range<i16>(HEIGHT - 1, -1)) {
                    if (y == 0) {
                        iadd_block({ x, u8(y), z }, BEDROCK);
                        continue;
                    }

                    f32 const cave_noise = noise->get_noise_3d(
                        global_x * CHEESE_CAVE.frequency,
                        y * CHEESE_CAVE.frequency,
                        global_z * CHEESE_CAVE.frequency
                    );

                    f32 const noise_3d = noise->get_noise_3d(
                        fsize(global_x) * 0.2f,
                        fsize(y) * 0.3f,
                        fsize(global_z) * 0.2f
                    );

                    f32 const density = terrain_base_y - f32(y) + (noise_3d * 25.0f);
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

                    iadd_block({ x, u8(y), z }, block_id);
                }
            }
        }

        {
            std::unique_lock lock(data_mutex);
            std::memcpy(blocks, new_blocks, sizeof(u8) * Chunk::WIDTH * Chunk::HEIGHT * Chunk::WIDTH);
            std::memcpy(block_ids, new_block_ids, sizeof(u32) * 256);

            block_ids_size = new_block_ids_size;
            id2block.swap(new_id2block);
            extended_block_id.swap(new_extended_block_id);

            meta_ids.swap(new_meta_ids);
            tag_ids.swap(new_tag_ids);
        }

        dirty.store(true, std::memory_order_release);
        generated.store(true, std::memory_order_release);
        ++chunk_version;
    }

    static u8 get_submesh_index(u8 y) { return std::clamp(y / (Chunk::HEIGHT / 4), 0, 3); }

    void Chunk::generate_mesh(ChunkRender& mesh, Ptr<Chunk> neighbors[4]) {
        Ptr<MeshesData> chunk_data_ptr = new Obj<MeshesData>();
        auto& chunk_data = chunk_data_ptr.value();

        for (auto i : range<u8>(4)) {
            auto& data = chunk_data[i];

            data.vertices.expect(1024);
            data.normals.expect(1024);
            data.indices.expect(1536);
            data.uvs.expect(1024);
            data.uvs_layer.expect(1024);
            data.collision_faces.expect(1536);
        }

        u32 const AIR = block_registry::get_id("Air");
        u32 const TRANSPARENT = tag_registry::get_id("transparent");

        List<std::shared_mutex*> mutexes_to_lock;
        mutexes_to_lock.append(&data_mutex);
        for (auto i : range<i32>(4)) if (neighbors[i]) mutexes_to_lock.append(&neighbors[i].value().data_mutex);

        std::ranges::sort(mutexes_to_lock);

        auto const result = std::ranges::unique(mutexes_to_lock);
        mutexes_to_lock.resize(result.begin() - mutexes_to_lock.begin());

        List<std::shared_lock<std::shared_mutex>> locks;
        for (auto* m : mutexes_to_lock) locks.emplace(std::shared_lock(*m));

        auto is_complex_block = [this, AIR](u32 id) -> bool {
            return block_registry::get_block(id).get_texture_layer(Face::TOP) == -1;
        };

        auto transparent = [this, &neighbors, AIR, TRANSPARENT, &is_complex_block](i32 bx, u8 by, i32 bz) -> bool {
            if (bx < Chunk::WIDTH and bx >= 0 and bz < Chunk::WIDTH and bz >= 0) {
                auto id = get_block({ u8(bx), by, u8(bz) });
                if (id == AIR or is_complex_block(id)) return true;

                auto tag_ptr = get_tag({ u8(bx), by, u8(bz) });
                return tag_ptr and tag_ptr->contains(TRANSPARENT);
            }

            u8 const nid = bx >= Chunk::WIDTH ? 0 :
                           bx < 0             ? 1 :
                           bz >= Chunk::WIDTH ? 2 :
                           bz < 0             ? 3 : 0;

            auto const& neighbor_ptr = neighbors[nid];
			if (not neighbor_ptr) return true;

			Chunk& neighbor = neighbor_ptr.value();
            if (not neighbor.generated.load(std::memory_order_acquire)) return true;

            u8 lx = u8((bx % Chunk::WIDTH + Chunk::WIDTH) % Chunk::WIDTH);
            u8 lz = u8((bz % Chunk::WIDTH + Chunk::WIDTH) % Chunk::WIDTH);

            auto const id = neighbor.get_block({ lx, by, lz });
            if (id == AIR or is_complex_block(id)) return true;

			auto const tag_ptr = neighbor.get_tag({ lx, by, lz });
            return tag_ptr and tag_ptr->contains(TRANSPARENT);
        };

        auto get_block_layer = [this, AIR, &is_complex_block](u8 bx, u8 by, u8 bz, Face face) -> i32 {
            u32 const id = get_block({ bx, by, bz });
            if (id == AIR or is_complex_block(id)) return -1;

            return block_registry::get_block(id).get_texture_layer(face);
        };

        static constexpr i64 dims[3] = { Chunk::WIDTH, Chunk::HEIGHT, Chunk::WIDTH };
        static constexpr Face front_faces[3] = { Face::RIGHT, Face::TOP,    Face::FRONT };
        static constexpr Face back_faces[3] =  { Face::LEFT,  Face::BOTTOM, Face::BACK  };

        List<FaceMask> mask;
        mask.resize(Chunk::HEIGHT * Chunk::WIDTH);

        u64 vertex_offsets[4] = {};
        for (i32 d : range(3)) {
            i32 const u = (d + 1) % 3;
            i32 const v = (d + 2) % 3;

            i32 x[3] = {};
            i32 q[3] = {};
            q[d] = 1;

            for (x[d] = -1; x[d] < dims[d]; ++x[d]) {
                mask.fill({ -1, false });

                for (x[v] = 0; x[v] < dims[v]; ++x[v]) {
                    for (x[u] = 0; x[u] < dims[u]; ++x[u]) {
                        bool const a_trans = transparent(x[0], x[1], x[2]);
                        bool const b_trans = transparent(x[0] + q[0], x[1] + q[1], x[2] + q[2]);

                        if (x[d] >= 0 and not a_trans and b_trans) {
                            i32 const layer = get_block_layer(x[0], x[1], x[2], front_faces[d]);
                            if (layer >= 0) mask[x[u] + x[v] * dims[u]] = { layer, false };
                        }
                        else if (x[d] + 1 < dims[d] and a_trans and not b_trans) {
                            i32 const layer = get_block_layer(x[0] + q[0], x[1] + q[1], x[2] + q[2], back_faces[d]);
                            if (layer >= 0) mask[x[u] + x[v] * dims[u]] = { layer, true };
                        }
                    }
                }

                for (auto j : range<i64>(dims[v])) {
                    i64 i = 0;
                    while (i < dims[u]) {
                        FaceMask const current_face = mask[i + j * dims[u]];
                        if (current_face.layer() < 0) {
                            ++i;
                            continue;
                        }

                        i32 width = 1;
                        while (i + width < dims[u] and mask[i + width + j * dims[u]] == current_face) ++width;

                        i32 height = 1;
                        while (j + height < dims[v]) {
                            for (i32 k : range(width)) {
                                if (mask[(i + k) + (j + height) * dims[u]] != current_face) goto CANT_GROW_MORE;
                            }
                            ++height;
                        }

                    CANT_GROW_MORE:

                        f32 du[3] = {}; du[u] = f32(width);
                        f32 dv[3] = {}; dv[v] = f32(height);

                        f32 start[3] = {};
                        start[d] = f32(x[d] + 1);
                        start[u] = f32(i);
                        start[v] = f32(j);

                        u8 const avg_y = u8(start[1]);
                        u8 const s_idx = get_submesh_index(avg_y);

                        auto& data = chunk_data[s_idx];
                        u64& vertex_offset = vertex_offsets[s_idx];

                        auto const p0 = Pos3D(start[0], start[1], start[2]);
                        auto const p1 = Pos3D(start[0] + du[0], start[1] + du[1], start[2] + du[2]);
                        auto const p2 = Pos3D(start[0] + du[0] + dv[0], start[1] + du[1] + dv[1], start[2] + du[2] + dv[2]);
                        auto const p3 = Pos3D(start[0] + dv[0], start[1] + dv[1], start[2] + dv[2]);

                        auto get_uv = [&p0, current_face, width, height, d](Pos3D<f32> const& p) -> Pos2D<fsize> {
                            f32 const dx = p.x - p0.x;
                            f32 const dy = p.y - p0.y;
                            f32 const dz = p.z - p0.z;

                            if (d == 0)      return Pos2D(current_face.back_face() ? height - dz : dz, width - dy);
                            else if (d == 1) return Pos2D(dx, current_face.back_face() ? height - dz : dz);
                            else             return Pos2D(current_face.back_face() ? width - dx : dx, height - dy);
                        };

                        auto& vertices        = data.vertices;
                        auto& normals         = data.normals;
                        auto& indices         = data.indices;
                        auto& uvs             = data.uvs;
                        auto& uvs_layer       = data.uvs_layer;
                        auto& collision_faces = data.collision_faces;

                        if (not current_face.back_face()) {
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

                        auto normal = Pos3D(0.0fz, 0.0fz, 0.0fz);

                        d == 0 ? normal.x :
                        d == 1 ? normal.y : normal.z = current_face.back_face() ? -1.0fz : 1.0fz;

                        for (auto n : range<i32>(4)) normals.append(normal);

                        Pos2D layer_uv(f32(current_face.layer()), 0.0f);
                        for (auto n : range<i32>(4)) uvs_layer.append(layer_uv);

                        indices.append(i32(vertex_offset)); indices.append(i32(vertex_offset + 2)); indices.append(i32(vertex_offset + 1));
                        indices.append(i32(vertex_offset)); indices.append(i32(vertex_offset + 3)); indices.append(i32(vertex_offset + 2));

                        collision_faces.append(vertices[vertex_offset    ]);
                        collision_faces.append(vertices[vertex_offset + 2]);
                        collision_faces.append(vertices[vertex_offset + 1]);
                        collision_faces.append(vertices[vertex_offset + 0]);
                        collision_faces.append(vertices[vertex_offset + 3]);
                        collision_faces.append(vertices[vertex_offset + 2]);

                        vertex_offset += 4;

                        for (i32 v_idx : range(height))
                            for (i32 u_idx : range(width))
                                mask[i + u_idx + (j + v_idx) * dims[u]] = { -1, false };

                        i += width;
                    }
                }
            }
        }

        for (u8 x : range(Chunk::WIDTH)) {
            for (u8 y : range(Chunk::HEIGHT)) {
                for (u8 z : range(Chunk::WIDTH)) {
                    u32 const id = get_block({ x, y, z });

                    if (not is_complex_block(id)) continue;
                    chunk_data[get_submesh_index(y)].complex_instance.append({ id, {x, y, z} });
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