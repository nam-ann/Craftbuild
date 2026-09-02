module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/classes/wrapped.hpp>
ENABLE_WARNING

#define VERSION "26.8"

export module game.core;

import std;

import misc.pos;
import misc.number;

using namespace godot;

export namespace craftbuild {
    inline constexpr char const* version = VERSION;
    inline constexpr char const* full_version = "indev " VERSION;
    inline bool craftbuild_debug = true;
    inline bool log_verbose = true;
    inline bool colored_log = true;

    inline int32 render_distance = 32;
    inline int32 cpu_sleep_time = 180;

    inline constexpr real MATH_PI = (real)Math_PI;
    inline constexpr real MAXIMUM_CAMERA_ANGLE = (real)2.0000002384185791015625f;
}