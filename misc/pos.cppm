module;

#pragma warning(push, 0)
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#pragma warning(pop)

#include <includes.hpp>
#include <xhash>

export module misc.pos;

import misc.format;
import misc.number;
import misc.hasher;

using namespace godot;

template<typename T1, typename T2>
concept AbleToCast = requires (T2 t2) {
    (T1)t2;
};

export namespace craftbuild {
    template <typename T>
    requires std::is_arithmetic_v<T>
    struct Pos3D {
        T x, y, z;

        Pos3D() = default;
        Pos3D(T x, T y, T z) : x(x), y(y), z(z) {}
        Pos3D(Vector3 const& v) : x(v.x), y(v.y), z(v.z) {}
        Pos3D(Vector3i const& v) : x(v.x), y(v.y), z(v.z) {}
        template<typename T2>
        requires AbleToCast<T, T2>
        Pos3D(Pos3D<T2> const& pos) : x((T)pos.x), y((T)pos.y), z((T)pos.z) {}

#define def_operator(op) Pos3D& operator##op##=(Pos3D const& other) { x op##= other.x, y op##= other.y, z op##= other.z; return *this;}

        def_operator(+);
        def_operator(-);
        def_operator(*);
        def_operator(/);
        def_operator(%);

#undef def_operator
#define def_operator(op) Pos3D operator##op(Pos3D const& other) const { Pos3D result; result.x op##= other.x, result.y op##= other.y, result.z op##= other.z; return result;}

        def_operator(+);
        def_operator(-);
        def_operator(*);
        def_operator(/);
        def_operator(%);

#undef def_operator

        operator godot::Vector3() const {
            return godot::Vector3(static_cast<float32>(x), static_cast<float32>(y), static_cast<float32>(z));
        }
        operator godot::Vector3i() const {
            return godot::Vector3i(static_cast<int32>(x), static_cast<int32>(y), static_cast<int32>(z));
        }

        bool operator==(Pos3D const& other) const {
            return x == other.x and y == other.y and z == other.z;
        }

        friend format&& operator<<(format&& f, Pos3D const& pos) {
            return std::move(f) << "(" << pos.x << ", " << pos.y << ", " << pos.z << ")";
        }
    };

    template <typename T>
    requires std::is_arithmetic_v<T>
    struct Pos2D {
        T x, y;

        Pos2D() = default;
        Pos2D(T x, T y) : x(x), y(y) {}
        Pos2D(Vector2 const& v) : x(v.x), y(v.y) {}
        Pos2D(Vector2i const& v) : x(v.x), y(v.y) {}
        template<typename T2>
        requires AbleToCast<T, T2>
        Pos2D(Pos2D<T2> const& pos) : x((T)pos.x), y((T)pos.y) {}

#define def_operator(op) Pos2D& operator##op##=(Pos2D const& other) { x op##= other.x, y op##= other.y; return *this;}

        def_operator(+);
        def_operator(-);
        def_operator(*);
        def_operator(/);
        def_operator(%);

#undef def_operator
#define def_operator(op) Pos2D operator##op(Pos2D const& other) const { Pos2D result; result.x op##= other.x, result.y op##= other.y; return result;}

        def_operator(+);
        def_operator(-);
        def_operator(*);
        def_operator(/);
        def_operator(%);

#undef def_operator

        operator godot::Vector2() const {
            return godot::Vector2(static_cast<float32>(x), static_cast<float32>(y));
        }
        operator godot::Vector2i() const {
            return godot::Vector2i(static_cast<int32>(x), static_cast<int32>(y));
        }

        bool operator==(Pos2D const& other) const {
            return x == other.x and y == other.y;
        }

		friend format&& operator<<(format&& f, Pos2D const& pos) {
			return std::move(f) << "(" << pos.x << ", " << pos.y << ")";
		}
    };

    template <typename T>
    requires std::is_arithmetic_v<T>
    struct Hasher<Pos3D<T>> {
        usize operator()(Pos3D<T> const& pos) const {
            return std::hash<T>{}(pos.x) ^ (std::hash<T>{}(pos.y) << 16) ^ (std::hash<T>{}(pos.z) << 8);
        }
    };

    template <typename T>
    requires std::is_arithmetic_v<T>
    struct Hasher<Pos2D<T>> {
        usize operator()(Pos2D<T> const& pos) const {
            return std::hash<T>{}(pos.x) ^ (std::hash<T>{}(pos.y) << 16);
        }
    };
}