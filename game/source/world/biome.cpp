module;

#include <includes.hpp>

module game.world.biome;

namespace craftbuild {
	void BiomeRegistry::register_biome(Str const& name, Biome const& biome) {
		name2id[name] = registry.size();
		registry.emplace_back(BiomeEntry(name, biome));
	}

	Biome BiomeRegistry::get_biome(uint64 biome_id) {
		if (registry.size() <= biome_id) return Biome{};
		return registry[biome_id].biome;
	}

	Str BiomeRegistry::get_name(uint64 biome_id) {
		if (registry.size() <= biome_id) return "";
		return registry[biome_id].name;
	}

	uint64 BiomeRegistry::get_id(Str const& biome_name) {
		if (name2id.find(biome_name) == name2id.end()) return 0;
		return name2id[biome_name];
	}
}