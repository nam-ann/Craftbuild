module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
ENABLE_WARNING

export module game.player;

import std;

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
        void* world_ptr = nullptr;
        Dictionary hit;
        MeshInstance3D* selection_box;

    protected:
        static void _bind_methods();

    public:
        void _ready() override;
        void _process(float64 delta) override;
        void _physics_process(float64 delta) override;
        void _input(Ref<InputEvent> const& event) override;

        bool would_collide_with_player(Pos3D<int32> const& block_pos) const;
        Ref<ShaderMaterial> create_selection_box_material();
        Dictionary raycast_block(real max_distance = 5.0f);
        Face get_face(Pos3D<real> n);

        void cycle_hotbar(int32 dir);
        void select_slot(int32 slot);
        uint32 get_selected_block_id() const;

        void save_data(std::ostream& os);
        void load_data(std::istream& is);
    };
}
