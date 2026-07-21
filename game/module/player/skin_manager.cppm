module;

#pragma warning(push, 0)
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#pragma warning(pop)

#include <includes.hpp>

export module game.player.skin_manager;

import misc.ptr;
import misc.number;
import misc.format;
import game.logger;

using namespace godot;

export namespace craftbuild {
    class Player;
    class SkinManager {
    public:
        static bool load_skin(Player& player, char const* path);
        static Ref<StandardMaterial3D> create_skin_material(Ref<Texture2D> texture);
        static void apply_skin_to_model(MeshInstance3D* model, Ref<Texture2D> texture);
    };
}