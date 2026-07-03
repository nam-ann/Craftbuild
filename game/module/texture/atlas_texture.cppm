module;

#pragma warning(push, 0)
#include <godot_cpp/classes/texture2d_array.hpp>
#pragma warning(pop)

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
    inline uint8 IMAGE_SIZE = 16;

	struct AtlasTexture {
		inline static Ref<Texture2DArray> atlas_texture;
		static none build_texture_array();
	};
}