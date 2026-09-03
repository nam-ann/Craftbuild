export module game.block.normal_blocks;

import std;

import misc.str;
import misc.number;
import game.block;
import game.block.block_data;

export namespace craftbuild {
	struct Air : public Block1F {};
	struct Dirt : public Block1F {};
	struct Grass : public Block3F {};
	struct Stone : public Block1F {};
	struct Pebble : public Block1F {};
	struct OakLog : public Block3F {};
	struct OakPlanks : public Block1F {};
	struct OakLeaves : public Block1F { Set<uint32> init_tags() override { return { TagRegistry::get_id("transparent") }; } };
	struct DiamondBlock : public Block1F {};
	struct DiamondOre : public Block1F {};
	struct Bedrock : public Block1F {};
}