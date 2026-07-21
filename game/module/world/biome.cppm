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
		inline static Dict<Str, uint64> name2id;

		static void register_biome(Str const& name, Biome const& biome);
		static Biome get_biome(uint64 biome_id);
		static Str get_name(uint64 biome_id);
		static uint64 get_id(Str const& biome_name);
	};
}