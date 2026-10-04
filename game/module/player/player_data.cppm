export module game.player.player_data;

import misc.pos;
import misc.str;
import misc.list;
import misc.dict;
import misc.number;

export namespace craftbuild {
    struct PlayerData final {
        Str name;
        Pos3D<fsize> pos;

        inline static constexpr u8 HOTBAR_SIZE = 9;
        u32 hotbar[HOTBAR_SIZE] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        u8 selected_slot = 0;
        Dict<Str, i32> inventory;
        i8 hp = 20;
    };
}