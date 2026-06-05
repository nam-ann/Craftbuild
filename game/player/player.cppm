module;

#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/character_body3d.hpp>

#include <includes.hpp>

export module game.player;

import misc.pos;
import misc.ptr;
import misc.str;
import misc.dict;
import misc.range;
import misc.number;
import misc.format;
import game.core;
import game.block;
import game.logger;
import game.world.chunk;
import game.texture.atlas_texture;

using namespace godot;

export namespace craftbuild {
    enum class Gamemode : uint8 { SURVIVAL, CREATIVE, ADVENTURE, SPECTATOR };

    class Player : public CharacterBody3D {
        GDCLASS(Player, CharacterBody3D)

    public:
        // Movement
        float32 speed = 4.0f;
        float32 gravity = 24.0f;
        float32 jump_velocity = 8.0f;
        bool is_grounded = false;
        bool can_fly = false;
        bool jump_was_pressed = false;
        bool running = false;
        Gamemode gamemode = Gamemode::SURVIVAL;

        // Hotbar
        inline static constexpr uint8 HOTBAR_SIZE = 9;
        uint32 hotbar[HOTBAR_SIZE] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        uint8 selected_slot = 0;

        // Camera
        Camera3D* camera = nullptr;
        float32 sensitivity = 0.0043f;
        float32 mouse_pitch = 0.0f;

        // World
        none* world_ptr = nullptr;

        // Gameplay
        int8 hp = 20;
        Dictionary hit;
        MeshInstance3D* selection_box;
        Dict<Str, int> inventory;

    protected:
        static none _bind_methods();

    public:
        none _ready() override;
        none _process(float64 delta) override;
        none _physics_process(float64 delta) override;
        none _input(const Ref<InputEvent>& event) override;

        bool would_collide_with_player(const Pos3D<int>& block_pos) const;
        Ref<ShaderMaterial> create_selection_box_material();
        Dictionary raycast_block(float max_distance = 5.0f);
        Face get_face(Pos3D<real> n);

        none cycle_hotbar(int dir);
        none select_slot(int slot);
        uint32 get_selected_block_id() const { return hotbar[selected_slot]; }

        none save_data(std::ostream& os);
        none load_data(std::istream& is);
    };
}
