module;

#include <godot_cpp/classes/texture2d.hpp>
#include <includes.hpp>

export module game.texture.asset_loader;

import misc.ptr;
import misc.str;
import misc.number;
import misc.format;
import game.logger;

using namespace godot;

export namespace craftbuild {
    enum class FaceCount : uint8 { ONE = 1, THREE = 3, SIX = 6 };

    struct AssetLoader {
        inline static Str base_path = "res://assets/textures/block/";
        static Ref<Texture2D> load_block_texture(usize id, const char* path_suffix, const FaceCount face_count);
    };
}