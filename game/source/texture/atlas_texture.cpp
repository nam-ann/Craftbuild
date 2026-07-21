module;

#pragma warning(push, 0)
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
#pragma warning(pop)

#include <includes.hpp>

module game.texture.atlas_texture;

namespace craftbuild {
    none AtlasTexture::build_texture_array() {
        Array images;
        int32 current_layer = 0;

        for (auto const& block : BlockRegistry::registry) {
            if (block.texture.is_null()) continue;

            Ref<Image> original_img = block.texture->get_image();
            if (original_img.is_null()) continue;

            block.block.value().base_texture_layer = current_layer;
            log<LogType::VERBOSE>(format{} << block.name.std_str().c_str() << " assigned layer: " << current_layer);

            int32 width = original_img->get_width();
            int32 face_count = width / IMAGE_SIZE;

            for (auto i : range<int32>(face_count)) {
                Ref<Image> tile = original_img->get_region(Rect2i(i * IMAGE_SIZE, 0, IMAGE_SIZE, IMAGE_SIZE));

                tile->convert(Image::FORMAT_RGBA8);
                tile->fix_alpha_edges();
                tile->generate_mipmaps();

                images.push_back(tile);
                current_layer++;
            }
        }

        if (images.size() == 0) return;

        atlas_texture.instantiate();
        Error err = atlas_texture->create_from_images(images);

        if (err != OK) log<LogType::ERROR>(format{} << "Error: create_from_images failed code: " << (int32)err);
        else log<LogType::VERBOSE>(format{} << "TextureArray build success: " << current_layer << " layers.");
    }
}