module;

#pragma warning(push, 0)
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#pragma warning(pop)

#include <includes.hpp>
#include <shared_mutex>

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
        real speed = 4.0f;
        real gravity = 24.0f;
        real jump_velocity = 8.0f;
        bool is_grounded = false;
        bool can_fly = false;
        bool jump_was_pressed = false;
        bool running = false;
        Gamemode gamemode = Gamemode::SURVIVAL;

        // Camera
        Camera3D* camera = nullptr;
        float32 sensitivity = 0.0043f;
        float32 mouse_pitch = 0.0f;
        
        // Gameplay
        inline static constexpr uint8 HOTBAR_SIZE = 9;
        uint32 hotbar[HOTBAR_SIZE] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        uint8 selected_slot = 0;
        Dict<Str, int32> inventory;
        int8 hp = 20;

        // World
        none* world_ptr = nullptr;
        Dictionary hit;
        MeshInstance3D* selection_box;

    protected:
        static none _bind_methods();

    public:
        none _ready() override;
        none _process(float64 delta) override;
        none _physics_process(float64 delta) override;
        none _input(const Ref<InputEvent>& event) override;

        bool would_collide_with_player(const Pos3D<int32>& block_pos) const;
        Ref<ShaderMaterial> create_selection_box_material();
        Dictionary raycast_block(real max_distance = 5.0f);
        Face get_face(Pos3D<real> n);

        none cycle_hotbar(int32 dir);
        none select_slot(int32 slot);
        uint32 get_selected_block_id() const;

        none save_data(std::ostream& os);
        none load_data(std::istream& is);
    };
}
