module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/core/math_defs.hpp>
ENABLE_WARNING

export module misc.number;

export namespace craftbuild {
	using i8 = signed char;
	using i16 = signed short;
	using i32 = signed int;
	using i64 = signed long long;
	using u8 = unsigned char;
	using u16 = unsigned short;
	using u32 = unsigned int;
	using u64 = unsigned long long;
	using f32 = float;
	using f64 = double;
	using f128 = long double;
	using fsize = godot::real_t;
	using usize = size_t;

	constexpr auto operator""fz(long double value) { return fsize(value); }
}

export using craftbuild::i8;
export using craftbuild::i16;
export using craftbuild::i32;
export using craftbuild::i64;
export using craftbuild::u8;
export using craftbuild::u16;
export using craftbuild::u32;
export using craftbuild::u64;
export using craftbuild::f32;
export using craftbuild::f64;
export using craftbuild::f128;
export using craftbuild::fsize;
export using craftbuild::usize;

export using craftbuild::operator""fz;