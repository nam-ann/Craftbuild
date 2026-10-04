module;

#include <defs.hpp>

module game.world.biome;

namespace craftbuild {
	namespace biome_registry {
		void register_biome(Str const& name, Biome const& biome) {
			name2id[name] = len(registry);
			registry.emplace(BiomeEntry(name, biome));
		}

		Biome get_biome(u64 biome_id) {
			if (len(registry) <= biome_id) return Biome{};
			return registry[biome_id].biome;
		}

		Str get_name(u64 biome_id) {
			if (len(registry) <= biome_id) return "";
			return registry[biome_id].name;
		}

		u64 get_id(Str const& biome_name) {
			if (name2id.find(biome_name) == name2id.end()) return 0;
			return name2id[biome_name];
		}
	}
}