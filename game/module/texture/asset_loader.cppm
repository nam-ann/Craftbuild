module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
ENABLE_WARNING

export module game.texture.asset_loader;

import misc.ptr;
import misc.str;
import misc.number;
import misc.format;
import game.logger;

using namespace godot;

export namespace craftbuild {
    enum class FaceCount : u8 { ONE = 1, THREE = 3, SIX = 6 };

    namespace asset_loader {
        inline Str base_path = "res://assets/textures/block/";
        Ref<Texture2D> load_block_texture(char const* path_suffix, const FaceCount face_count);
        Ref<PackedScene> load_block_model(char const* path_suffix);
    };
}