module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/texture2d_array.hpp>
ENABLE_WARNING

export module game.texture.atlas_texture;

import misc.range;
import misc.number;
import misc.format;
import misc.ptr;
import game.block;
import game.logger;

using namespace godot;

export namespace craftbuild {
    inline u8 IMAGE_SIZE = 16;

	namespace atlas_texture {
		inline Ref<Texture2DArray> atlas_texture;
		void build_texture_array();
	};
}