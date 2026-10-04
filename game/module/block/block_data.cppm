export module game.block.block_data;

import std;

import misc.str;
import misc.list;
import misc.dict;
import misc.number;

export namespace craftbuild {
	namespace meta_registry {
		inline List<Str> registry;
		inline Dict<Str, u32> name2id;

		void register_metadata(Str const& name);
		Str& get_metadata(u32 meta_id);
		u32 get_id(Str const& meta_name);
		bool has_metadata(Str const& meta_name);
	}

	namespace tag_registry {
		inline List<Str> registry;
		inline Dict<Str, u32> name2id;

		void register_tag(Str const& name);
		Str& get_tag(u32 tag_id);
		u32 get_id(Str const& tag_name);
		bool has_tag(Str const& tag_name);
	}
}