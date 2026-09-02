export module game.block.metadata;

import std;

import misc.str;
import misc.dict;
import misc.number;

export namespace craftbuild {
	struct MetaRegistry {
		inline static std::vector<Str> registry;
		inline static Dict<Str, uint64> name2id;

		static void register_metadata(Str const& name);
		static Str& get_metadata(uint64 meta_id);
		static uint64 get_id(Str const& meta_name);
		static bool has_metadata(Str const& meta_name);
	};

	struct MetaStorage {
		uint64 id;
		Str data;
	};
}