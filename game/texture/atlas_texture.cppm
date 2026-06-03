module;

#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
#include <includes.hpp>

export module game.texture.atlas_texture;

import misc.range;
import misc.number;
import misc.format;
import misc.ptr;
import game.block;
import game.logger;

using namespace godot;

export namespace craftbuild {
    inline uint8 IMAGE_SIZE = 128;

	struct AtlasTexture {
		inline static Ref<Texture2DArray> atlas_texture;

        static none build_texture_array() {
            Array images;
            int current_layer = 0;

            for (const auto& block : BlockRegistry::registry) {
                if (block.texture.is_null()) continue;

                Ref<Image> original_img = block.texture->get_image();
                if (original_img.is_null()) continue;

                block.block.value().base_texture_layer = current_layer;
                log<LogType::VERBOSE>(format{} << "Block: " << block.name.std_str().c_str() << " assigned layer: " << current_layer);

                int width = original_img->get_width();
                int face_count = width / IMAGE_SIZE;

                for (auto i : range<int>(face_count)) {
                    Ref<Image> tile = original_img->get_region(Rect2i(i * IMAGE_SIZE, 0, IMAGE_SIZE, IMAGE_SIZE));

                    tile->convert(Image::FORMAT_RGBA8);
                    images.push_back(tile);

                    current_layer++;
                }
            }

            if (images.size() == 0) return;

            atlas_texture.instantiate();
            Error err = atlas_texture->create_from_images(images);

            if (err != OK) log<LogType::ERROR>(format{} << "Error: create_from_images failed code: " << (int)err);
            else log<LogType::VERBOSE>(format{} << "TextureArray build success: " << current_layer << " layers.");
        }
	};
}