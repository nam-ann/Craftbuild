module;

#pragma warning(push, 0)
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#pragma warning(pop)

#include <includes.hpp>
#include <concepts>
#define VERSION "26.8"

export module game.core;

using namespace godot;

import misc.pos;
import misc.number;

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

    inline uint64 pack_vec3_mm(Pos3D<real> const& v) {
        return (uint64)(uint16)(v.x * 1000.0f)
            | ((uint64)(uint16)(v.y * 1000.0f) << 16)
            | ((uint64)(uint16)(v.z * 1000.0f) << 32);
    }

    inline Pos3D<real> unpack_vec3_mm(uint64 packed) {
        uint16 x = packed & 0xFFFF;
        uint16 y = (packed >> 16) & 0xFFFF;
        uint16 z = (packed >> 32) & 0xFFFF;
        return Pos3D<real>(x / 1000.0f, y / 1000.0f, z / 1000.0f);
    }
}