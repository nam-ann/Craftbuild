module;

#include <includes.hpp>

export module game.world.biome;

import misc.str;
import misc.dict;
import misc.number;

export namespace craftbuild {
	struct Biome {
		float32 base_noise = 0.0f;
		float32 base_height = 0.0f;
		float32 detail_noise = 0.0f;
		float32 detail_height = 0.0f;
		float32 temperature = 0.0f;
		int32 min_height = 0;
	};

	struct BiomeEntry {
		Str name;
		Biome biome;
	};

	struct BiomeRegistry {
		inline static std::vector<BiomeEntry> registry;
		inline static Dict<Str, size> name2id;

		static none register_biome(const Str& name, const Biome& biome);
		static Biome get_biome(size biome_id);
		static Str get_name(size biome_id);
		static size get_id(const Str& biome_name);
	};
}