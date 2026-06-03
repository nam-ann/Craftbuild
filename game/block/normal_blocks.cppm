module;

#include <includes.hpp>

export module game.block.normal_blocks;

import misc.str;
import misc.number;
import game.block;

export namespace craftbuild {
	class Air : public Block1F {
	public:
		std::vector<std::pair<Str, size>> init_tags() override {
			return { { "transparent", 1 } };
		}
	};
	class Dirt : public Block1F {};
	class Grass : public Block3F {};
	class Stone : public Block1F {};
	class Pebble : public Block1F {};
	class OakLog : public Block3F {};
	class OakPlanks : public Block1F {};
	class OakLeaves : public Block1F {};
	class DiamondBlock : public Block1F {};
	class DiamondOre : public Block1F {};
	class Bedrock : public Block1F {};
}