module;

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
    inline uint16 IMAGE_SIZE = 256;

	struct AtlasTexture {
		inline static Ref<Texture2DArray> atlas_texture;
		static none build_texture_array();
	};
}