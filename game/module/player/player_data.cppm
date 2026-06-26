export module game.player.player_data;

import misc.pos;
import misc.str;
import misc.list;
import misc.dict;
import misc.number;

export namespace craftbuild {
    struct PlayerData {
        Str name;
        Pos3D<real> pos;

        inline static constexpr uint8 HOTBAR_SIZE = 9;
        uint32 hotbar[HOTBAR_SIZE] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        uint8 selected_slot = 0;
        Dict<Str, int> inventory;
        int8 hp = 20;
    };
}