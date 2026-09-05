module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/capsule_shape3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>
#include <godot_cpp/variant/dictionary.hpp>
ENABLE_WARNING

module game.player;

import game.main;
import game.player.skin_manager;

namespace craftbuild {
    bool SkinManager::load_skin(Player& player, char const* path) {
        Ref<Texture2D> skin_tex = ResourceLoader::get_singleton()->load(path);
        if (skin_tex.is_null()) {
            log<LogType::ERROR>("Failed to load skin: "f << path);
            return false;
        }

        log<LogType::INFO>("Skin loaded: "f << path);

        MeshInstance3D* player_model = player.get_node<MeshInstance3D>("Mesh");
        if (player_model) SkinManager::apply_skin_to_model(player_model, skin_tex);
        else log<LogType::WARNING>("Player model not found. Create a MeshInstance3D named 'Model'");

        return true;
    }

    void Player::_ready() {
        // Camera
        camera = get_node<Camera3D>("Camera");
        camera->set_position(Vector3(0, 1.8f, 0));

        // Model
        auto* model = get_node<MeshInstance3D>("Mesh");

        Ref<BoxMesh> box = memnew(BoxMesh);
        box->set_size(Vector3(0.6f, 2.0f, 0.3f));

        model->set_mesh(box);
        model->set_position(Vector3(0, 1.0f, 0));

        // Collision
        auto* collision = get_node<CollisionShape3D>("Shape");

        Ref<CapsuleShape3D> capsule = memnew(CapsuleShape3D);
        capsule->set_radius(0.3f);
        capsule->set_height(1.8f);

        collision->set_shape(capsule);
        collision->set_position(Vector3(0, 1.0f, 0));

        // Selection box
        selection_box = memnew(MeshInstance3D);
        selection_box->set_scale(Vector3(1.001f, 1.001f, 1.001f));

        Ref<BoxMesh> selection_mesh = memnew(BoxMesh);
        selection_mesh->set_size(Vector3(1.001f, 1.001f, 1.001f));

        selection_box->set_mesh(selection_mesh);
        selection_box->set_material_override(create_selection_box_material());

        // Load skin
        SkinManager::load_skin(*this, "res://assets/textures/skin/creeper_boy.png");
        
		// World reference
        world_ptr = Object::cast_to<Main>(get_parent());

        log<LogType::INFO>("Player initialized");
    }

    void Player::_process(float64 delta) {
        Main* world = static_cast<Main*>(world_ptr);
        if (not world) return;
        if (world->pausing.load(std::memory_order_relaxed)) return;

        hit = raycast_block();
        if (hit.is_empty()) {
            if (selection_box->get_parent() == world) world->remove_child(selection_box);
            return;
        }
        if (selection_box->get_parent() != world) world->add_child(selection_box);

        Vector3 const hit_pos = hit["position"];
        Vector3 const normal = hit["normal"];

        Vector3 const pos_float = hit_pos - (normal * 0.001f);
        Pos3D<real> block_pos = pos_float.floor();

        selection_box->set_position(Vector3(block_pos.x, block_pos.y, block_pos.z) + Vector3(0.5, 0.5, 0.5));
    }

    void Player::_physics_process(float64 delta) {
        if (gamemode == Gamemode::SPECTATOR) return;
        if (not camera or not world_ptr) return;

		Main* world = static_cast<Main*>(world_ptr);
        if (world->pausing.load(std::memory_order_relaxed)) return;

        Vector3 velocity = get_velocity();
        Input* input = Input::get_singleton();

        bool const chatting = world->chatting.load(std::memory_order_relaxed);
        bool const key_space = chatting ? false : input->is_key_pressed(KEY_SPACE);
        bool const key_shift = chatting ? false : input->is_key_pressed(KEY_SHIFT);
        bool const key_ctrl = chatting ? false : input->is_key_pressed(KEY_CTRL);
        bool const key_w = chatting ? false : input->is_key_pressed(KEY_W);
        bool const key_d = chatting ? false : input->is_key_pressed(KEY_D);
        bool const key_s = chatting ? false : input->is_key_pressed(KEY_S);
        bool const key_a = chatting ? false : input->is_key_pressed(KEY_A);

        is_grounded = is_on_floor();

        running = key_w and (key_ctrl or running);
        float32 const current_speed = running ? speed * 2.0f : speed;

        Vector3 forward = -camera->get_global_transform().basis.get_column(2);
        Vector3 right = camera->get_global_transform().basis.get_column(0);

        float32 forward_input = (key_w ? 1.0f : 0.0f) - (key_s ? 1.0f : 0.0f);
        float32 strafe_input = (key_d ? 1.0f : 0.0f) - (key_a ? 1.0f : 0.0f);

        Vector3 wish_dir = (forward * forward_input) + (right * strafe_input);
        wish_dir.y = 0;
        if (wish_dir.length() > 0) wish_dir = wish_dir.normalized();

        float32 const accel = is_grounded ? 18.0f : 6.0f;
        float32 const decel = is_grounded ? 22.0f : 2.0f;

        // Gravity & Jump
        if (not can_fly) {
            bool const has_input = wish_dir.length_squared() > 0.0f;
            float32 const blend = has_input ? accel : decel;

            Vector3 const target_xz = wish_dir * current_speed;
            velocity.x = velocity.x + (target_xz.x - velocity.x) * real(std::min(blend * delta, 1.0));
            velocity.z = velocity.z + (target_xz.z - velocity.z) * real(std::min(blend * delta, 1.0));

            if (is_grounded) {
                if (velocity.y < 0.0f) velocity.y = -0.1f;
                if (key_space) velocity.y = jump_velocity;
                if (gamemode == Gamemode::CREATIVE and key_space and not jump_was_pressed) {
                    can_fly = true;
                    velocity.y = 0.0f;
                }
            }
            else {
                if (gamemode == Gamemode::CREATIVE and key_space and not jump_was_pressed) {
                    can_fly = true;
                    velocity.y = 0.0f;
                }
                else velocity.y -= real(gravity * delta);
            }
        }
        else {
            bool const has_h_input = wish_dir.length_squared() > 0.0f;
            float32 const h_blend = has_h_input ? accel : decel;
            Vector3 const target_xz = wish_dir * current_speed;
            velocity.x = velocity.x + (target_xz.x - velocity.x) * real(std::min(h_blend * delta, 1.0));
            velocity.z = velocity.z + (target_xz.z - velocity.z) * real(std::min(h_blend * delta, 1.0));

            float32 wish_y = 0.0f;
            if (key_space) wish_y = speed;
            if (key_shift) wish_y = -speed;

            bool const has_v_input = (wish_y != 0.0f);
            float32 const v_blend = has_v_input ? accel : decel;
            velocity.y = velocity.y + (wish_y - velocity.y) * real(std::min(v_blend * delta, 1.0));

            if (is_grounded and not key_space) can_fly = false;
        }

        jump_was_pressed = key_space;

        set_velocity(velocity);
        move_and_slide();
    }

    void Player::_input(Ref<InputEvent> const& event) {
        if (not camera or not world_ptr) return;
        Main* world = static_cast<Main*>(world_ptr);
        if (world->pausing.load(std::memory_order_relaxed) or world->chatting.load(std::memory_order_relaxed)) return;

        static bool gamemode_toggled = false;

        // Mouse look
        if (auto mm = Object::cast_to<InputEventMouseMotion>(event.ptr())) {
            Vector2 rel = mm->get_relative();
            rotate_y(-rel.x * sensitivity);
            mouse_pitch = std::clamp(mouse_pitch - rel.y * sensitivity, -MATH_PI / MAXIMUM_CAMERA_ANGLE, MATH_PI / MAXIMUM_CAMERA_ANGLE);
            camera->set_rotation(Vector3(mouse_pitch, 0, 0));
        }

        // Mouse click/roll
        if (auto mb = Object::cast_to<InputEventMouseButton>(event.ptr())) {
            if (mb->is_pressed()) {
                // Mouse roll
                if (mb->get_button_index() == MOUSE_BUTTON_WHEEL_UP)   cycle_hotbar(-1);
                if (mb->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN) cycle_hotbar(1);
                
                // Mouse click
                bool mid = mb->get_button_index() == MOUSE_BUTTON_MIDDLE;
                bool left = mb->get_button_index() == MOUSE_BUTTON_LEFT;
                bool right = mb->get_button_index() == MOUSE_BUTTON_RIGHT;

                if (not hit.is_empty()) {
                    Vector3 const hit_pos = hit["position"];
                    Vector3 const normal = hit["normal"];

                    Vector3 const pos_float = hit_pos - (normal * 0.001f);
                    Vector3i block_pos = Vector3i(pos_float.floor());

                    uint32 target_block_id = world->get_global_block_id(block_pos.x, block_pos.y, block_pos.z);
                    log<LogType::VERBOSE>("Looking at block id: "f << target_block_id << " at (" << block_pos.x << ", " << block_pos.y << ", " << block_pos.z << ")");

                    if (mid and gamemode == Gamemode::CREATIVE) hotbar[selected_slot] = target_block_id;

                    if (left or right) {
                        uint32 AIR = BlockRegistry::get_id("Air");
                        uint32 block = get_selected_block_id();

                        if (left) world->set_global_block_id(AIR, block_pos.x, block_pos.y, block_pos.z);
                        if (right and block != AIR) {
                            Face face = get_face(normal);
                            switch (face) {
                            case Face::TOP:    block_pos.y += 1;  break;
                            case Face::BOTTOM: block_pos.y += -1; break;
                            case Face::LEFT:   block_pos.x += 1;  break;
                            case Face::RIGHT:  block_pos.x += -1; break;
                            case Face::FRONT:  block_pos.z += 1;  break;
                            case Face::BACK:   block_pos.z += -1; break;
                            }

                            if (not would_collide_with_player(block_pos)) world->set_global_block_id(block, block_pos.x, block_pos.y, block_pos.z);
                        }

                        const auto cx = int32(std::floor(float32(block_pos.x) / Chunk::WIDTH));
                        const auto cy = int32(std::floor(float32(block_pos.z) / Chunk::WIDTH));
                        if (auto chunk = world->get_chunk(cx, cy)) {
                            chunk.value().dirty.store(true, std::memory_order_release);

                            if (block_pos.x >= 0 or block_pos.z >= 0 or block_pos.x < Chunk::WIDTH or block_pos.z < Chunk::WIDTH) {
                                Pos2D<int32> neighbor_offsets[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
                                for (auto const& offset : neighbor_offsets) {
                                    if (auto neighbor = world->get_chunk(cx + offset.x, cy + offset.y)) neighbor.value().dirty.store(true, std::memory_order_release);
                                }
                            }
                        }
                    }
                }
            }
        }

        if (auto input = Object::cast_to<InputEventKey>(event.ptr())) {
            bool is_f3_held = Input::get_singleton()->is_key_pressed(KEY_F3);

            if (input->is_pressed() and input->get_keycode() == KEY_F4 and is_f3_held) {
                if (not gamemode_toggled) {
                    gamemode = (Gamemode)(((uint8_t)gamemode + 1) % 4);
                    log<LogType::INFO>("Changed gamemode to "f << (int32)gamemode);
                    can_fly = false;
                    gamemode_toggled = true;
                }
            }
            else if (not input->is_pressed() and input->get_keycode() == KEY_F4) gamemode_toggled = false;
            if (gamemode == Gamemode::SPECTATOR) can_fly = true;
        }

        for (auto i : range<int32>(9)) {
            Key key = (Key)(KEY_1 + i);
            if (Input::get_singleton()->is_key_pressed(key)) {
                if (event->is_pressed() and not event->is_echo()) {
                    select_slot(i);
                }
            }
        }
    }

    bool Player::would_collide_with_player(Pos3D<int32> const& block_pos) const {
        Pos3D<real> player_pos = get_position();

        real min_x = player_pos.x - 0.3f;
        real max_x = player_pos.x + 0.3f;
        real min_y = player_pos.y;
        real max_y = player_pos.y + 1.8f;
        real min_z = player_pos.z - 0.3f;
        real max_z = player_pos.z + 0.3f;

        real block_min_x = real(block_pos.x);
        real block_max_x = real(block_pos.x + 1.0);
        real block_min_y = real(block_pos.y);
        real block_max_y = real(block_pos.y + 0.8);
        real block_min_z = real(block_pos.z);
        real block_max_z = real(block_pos.z + 1.0);

        return (max_x > block_min_x and min_x < block_max_x and max_y > block_min_y and min_y < block_max_y and max_z > block_min_z and min_z < block_max_z);
    }

    Ref<ShaderMaterial> Player::create_selection_box_material() {
        Ref<ShaderMaterial> mat;
        mat.instantiate();

        String shader_path = "res://assets/shaders/selection_box.gdshader";

        Ref<Shader> shader;
        shader.instantiate();

        if (FileAccess::file_exists(shader_path)) {
            Ref<FileAccess> file = FileAccess::open(shader_path, FileAccess::READ);
            if (file.is_valid()) {
                String shader_code = file->get_as_text();
                shader->set_code(shader_code);
            }
            else log<LogType::ERROR>("Failed to open shader file at: "f << shader_path.utf8());
        }
        else log<LogType::ERROR>("Shader file not found at: "f << shader_path.utf8());

        mat->set_shader(shader);
        return mat;
    }

    Dictionary Player::raycast_block(real max_distance) {
        if (not camera) return Dictionary();

        Vector2 screen_center = camera->get_viewport()->get_visible_rect().get_center();
        Vector3 origin = camera->project_ray_origin(screen_center);
        Vector3 direction = camera->project_ray_normal(screen_center);
        Vector3 end = origin + direction * max_distance;

        PhysicsDirectSpaceState3D* space_state = camera->get_world_3d()->get_direct_space_state();
        Ref<PhysicsRayQueryParameters3D> query = PhysicsRayQueryParameters3D::create(origin, end);

        query->set_collision_mask(1); 

        return space_state->intersect_ray(query);
    }

    Face Player::get_face(Pos3D<real> n) {
        if (n == Pos3D<real>(0, 1, 0))  return Face::TOP;
        if (n == Pos3D<real>(0, -1, 0)) return Face::BOTTOM;
        if (n == Pos3D<real>(1, 0, 0))  return Face::LEFT;
        if (n == Pos3D<real>(-1, 0, 0)) return Face::RIGHT;
        if (n == Pos3D<real>(0, 0, 1))  return Face::FRONT;
        if (n == Pos3D<real>(0, 0, -1)) return Face::BACK;
        return Face::TOP;
    }

    void Player::cycle_hotbar(int32 dir) {
        selected_slot = (selected_slot + dir + HOTBAR_SIZE) % HOTBAR_SIZE;
        log<LogType::INFO>("Selected slot: "f << selected_slot + 1);
    }

    void Player::select_slot(int32 slot) {

    }

    uint32 Player::get_selected_block_id() const {
        return hotbar[selected_slot];
    }

    void Player::save_data(std::ostream& os) {
        os.write(reinterpret_cast<char const*>(&speed), sizeof(float32));
        os.write(reinterpret_cast<char const*>(&gravity), sizeof(float32));
        os.write(reinterpret_cast<char const*>(&jump_velocity), sizeof(float32));
        os.write(reinterpret_cast<char const*>(&is_grounded), sizeof(bool));
        os.write(reinterpret_cast<char const*>(&can_fly), sizeof(bool));
        os.write(reinterpret_cast<char const*>(&running), sizeof(bool));
        os.write(reinterpret_cast<char const*>(&gamemode), sizeof(Gamemode));
        os.write(reinterpret_cast<char const*>(&hotbar), sizeof(uint32) * HOTBAR_SIZE);
        os.write(reinterpret_cast<char const*>(&selected_slot), sizeof(uint8));
    }

    void Player::load_data(std::istream& is) {
        is.read(reinterpret_cast<char*>(&speed), sizeof(float32));
        is.read(reinterpret_cast<char*>(&gravity), sizeof(float32));
        is.read(reinterpret_cast<char*>(&jump_velocity), sizeof(float32));
        is.read(reinterpret_cast<char*>(&is_grounded), sizeof(bool));
        is.read(reinterpret_cast<char*>(&can_fly), sizeof(bool));
        is.read(reinterpret_cast<char*>(&running), sizeof(bool));
        is.read(reinterpret_cast<char*>(&gamemode), sizeof(Gamemode));
        is.read(reinterpret_cast<char*>(&hotbar), sizeof(uint32) * HOTBAR_SIZE);
        is.read(reinterpret_cast<char*>(&selected_slot), sizeof(uint8));
    }

    void Player::_bind_methods() {}
}
