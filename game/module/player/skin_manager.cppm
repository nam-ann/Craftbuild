module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
ENABLE_WARNING

export module game.player.skin_manager;

import misc.ptr;
import misc.number;
import misc.format;
import game.logger;
import game.player;

using namespace godot;

export namespace craftbuild {
    namespace skin_manager {
        bool load_skin(Player& player, char const* path);
        Ref<StandardMaterial3D> create_skin_material(Ref<Texture2D> texture);
        void apply_skin_to_model(MeshInstance3D* model, Ref<Texture2D> texture);
    };
}