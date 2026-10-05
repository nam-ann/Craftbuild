module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
ENABLE_WARNING

module game.texture.asset_loader;

namespace craftbuild {
    namespace asset_loader {
        Ref<Texture2D> load_block_texture(char const* path_suffix, const FaceCount face_count) {
            if (not path_suffix or std::strlen(path_suffix) == 0) return Ref<Texture2D>();

            String full_path = base_path.std_str().c_str();

            if (face_count == FaceCount::ONE)        full_path += "1f/";
            else if (face_count == FaceCount::THREE) full_path += "3f/";
            else if (face_count == FaceCount::SIX)   full_path += "6f/";

            full_path += path_suffix;

            Ref<Texture2D> tex = ResourceLoader::get_singleton()->load(full_path);
            if (tex.is_valid()) {
                log<LogType::VERBOSE>("Loaded: \""f << full_path.utf8() << "\"");
                return tex;
            }
            else log<LogType::ERROR>("Failed to load: \""f << full_path.utf8() << "\"");
            return Ref<Texture2D>();
        }

        Ref<PackedScene> load_block_model(char const* path_suffix) {
            if (not path_suffix or std::strlen(path_suffix) == 0) return Ref<PackedScene>();

            String full_path = Str(""f << base_path << "dynamic/" << path_suffix).std_str().c_str();

            Ref<PackedScene> model = ResourceLoader::get_singleton()->load(full_path);
            if (model.is_valid()) {
                log<LogType::VERBOSE>("Loaded: \""f << full_path.utf8() << "\"");
                return model;
            }
            else log<LogType::ERROR>("Failed to load: \""f << full_path.utf8() << "\"");

            return Ref<PackedScene>();
        }
    }
}