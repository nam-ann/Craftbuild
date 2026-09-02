module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/core/math_defs.hpp>
ENABLE_WARNING

export module misc.number;

import std;

export namespace craftbuild {
	using int8 = std::int8_t;
	using int16 = std::int16_t;
	using int32 = std::int32_t;
	using int64 = std::int64_t;
	using uint8 = std::uint8_t;
	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;
	using uint64 = std::uint64_t;
	using byte32 = char32_t;
	using float32 = float;
	using float64 = double;
	using real = godot::real_t;
	using usize = size_t;
}

export using craftbuild::int8;
export using craftbuild::int16;
export using craftbuild::int32;
export using craftbuild::int64;
export using craftbuild::uint8;
export using craftbuild::uint16;
export using craftbuild::uint32;
export using craftbuild::uint64;
export using craftbuild::byte32;
export using craftbuild::float32;
export using craftbuild::float64;
export using craftbuild::real;
export using craftbuild::usize;