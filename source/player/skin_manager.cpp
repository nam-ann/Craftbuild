module;

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

#include <includes.hpp>

module game.player.skin_manager;

import game.player;

namespace craftbuild {
    Ref<StandardMaterial3D> SkinManager::create_skin_material(Ref<Texture2D> texture) {
        Ref<StandardMaterial3D> mat;
        mat.instantiate();
        mat->set_texture(StandardMaterial3D::TEXTURE_ALBEDO, texture);
        mat->set_flag(StandardMaterial3D::FLAG_ALBEDO_TEXTURE_FORCE_SRGB, true);
        mat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA_DEPTH_PRE_PASS); // Support layer 2
        return mat;
    }

    none SkinManager::apply_skin_to_model(MeshInstance3D* model, Ref<Texture2D> texture) {
        if (not model) return;

        Ref<StandardMaterial3D> mat = create_skin_material(texture);
        model->set_material_override(mat);
        log<LogType::VERBOSE>("Skin applied to model");
    }
}