module;

#pragma warning(push, 0)
#include <godot_cpp/core/math_defs.hpp>
#pragma warning(pop)

#include <includes.hpp>
#include <cstdint>

export module misc.number;

export using int8 = int8_t;
export using int16 = int16_t;
export using int32 = int32_t;
export using int64 = int64_t;
export using uint8 = uint8_t;
export using uint16 = uint16_t;
export using uint32 = uint32_t;
export using uint64 = uint64_t;
export using byte = char;
export using ubyte = unsigned char;
export using byte32 = char32_t;
export using float32 = float;
export using float64 = double;
export using real = godot::real_t;
export using usize = size_t;
export using none = void;