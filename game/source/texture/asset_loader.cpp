module;

#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>

#include <includes.hpp>

module game.texture.asset_loader;

namespace craftbuild {
    Ref<Texture2D> AssetLoader::load_block_texture(usize id, const char* path_suffix, const FaceCount face_count) {
        if (path_suffix == nullptr or not Str(path_suffix)) {
            return Ref<Texture2D>();
        }

        String full_path = base_path.std_str().c_str();

        if (face_count == FaceCount::ONE)        full_path += "1 face/";
        else if (face_count == FaceCount::THREE) full_path += "3 faces/";
        else if (face_count == FaceCount::SIX)   full_path += "6 faces/";

        full_path += path_suffix;

        Ref<Texture2D> tex = ResourceLoader::get_singleton()->load(full_path);
        if (tex.is_valid()) {
            log<LogType::VERBOSE>(format{} << "Loaded texture: " << full_path.ascii());
            return tex;
        }
        else log<LogType::ERROR>(format{} << "Failed to load texture: " << full_path.ascii());
        return Ref<Texture2D>();
    }
}