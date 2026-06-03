module;

#include <godot_cpp/classes/wrapped.hpp>

#include <includes.hpp>
#include <concepts>
#define VERSION "26.4"

export module game.core;

using namespace godot;

import misc.number;

export namespace craftbuild {
    inline constexpr const char* version = VERSION;
    inline constexpr const char* full_version = "indev " VERSION;
    inline bool craftbuild_debug = true;
    inline bool log_verbose = true;
    inline bool colored_log = true;

    inline int32 render_distance = 32;
    inline int32 sleep_time_cpu = 180;
}