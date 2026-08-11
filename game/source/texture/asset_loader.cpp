module;

#include <defs.hpp>

NO_WARNING
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
DO_WARNING

module game.texture.asset_loader;

namespace craftbuild {
    Ref<Texture2D> AssetLoader::load_block_texture(char const* path_suffix, const FaceCount face_count) {
        if (path_suffix == nullptr or not Str(path_suffix)) {
            return Ref<Texture2D>();
        }

        String full_path = base_path.std_str().c_str();

        if (face_count == FaceCount::ONE)        full_path += "1f/";
        else if (face_count == FaceCount::THREE) full_path += "3f/";
        else if (face_count == FaceCount::SIX)   full_path += "6f/";

        full_path += path_suffix;

        Ref<Texture2D> tex = ResourceLoader::get_singleton()->load(full_path);
        if (tex.is_valid()) {
            log<LogType::VERBOSE>(format{} << "Loaded: \"" << full_path.ascii() << "\"");
            return tex;
        }
        else log<LogType::ERROR>(format{} << "Failed to load: \"" << full_path.ascii() << "\"");
        return Ref<Texture2D>();
    }

    Ref<PackedScene> AssetLoader::load_block_model(char const* path_suffix) {
        if (path_suffix == nullptr or not Str(path_suffix)) {
            return Ref<Texture2D>();
        }

        String full_path = (base_path.std_str() + "dynamic/" + path_suffix).c_str();

        Ref<PackedScene> model = ResourceLoader::get_singleton()->load(full_path);
        if (model.is_valid()) {
            log<LogType::VERBOSE>(format{} << "Loaded: \"" << full_path.ascii() << "\"");
            return model;
        }
        else log<LogType::ERROR>(format{} << "Failed to load: \"" << full_path.ascii() << "\"");
        return Ref<PackedScene>();
    }
}