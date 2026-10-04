export module game.world.biome;

import misc.str;
import misc.list;
import misc.dict;
import misc.number;

export namespace craftbuild {
	struct Biome final {
		f32 base_noise = 0.0f;
		f32 base_height = 0.0f;
		f32 detail_noise = 0.0f;
		f32 detail_height = 0.0f;
		f32 temperature = 0.0f;
		i32 min_height = 0;
	};

	struct BiomeEntry final {
		Str name;
		Biome biome;
	};

	namespace biome_registry {
		inline List<BiomeEntry> registry;
		inline Dict<Str, u64> name2id;

		void register_biome(Str const& name, Biome const& biome);
		Biome get_biome(u64 biome_id);
		Str get_name(u64 biome_id);
		u64 get_id(Str const& biome_name);
	};
}