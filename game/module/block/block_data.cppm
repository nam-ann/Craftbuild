export module game.block.block_data;

import std;

import misc.str;
import misc.dict;
import misc.number;

export namespace craftbuild {
	struct MetaRegistry {
		inline static std::vector<Str> registry;
		inline static Dict<Str, uint32> name2id;

		static void register_metadata(Str const& name);
		static Str& get_metadata(uint32 meta_id);
		static uint32 get_id(Str const& meta_name);
		static bool has_metadata(Str const& meta_name);
	};

	struct TagRegistry {
		inline static std::vector<Str> registry;
		inline static Dict<Str, uint32> name2id;

		static void register_tag(Str const& name);
		static Str& get_tag(uint32 tag_id);
		static uint32 get_id(Str const& tag_name);
		static bool has_tag(Str const& tag_name);
	};
}