module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/ref.hpp>
ENABLE_WARNING

export module misc.hasher;

import std;
import misc.number;

export namespace craftbuild {
    template <typename T>
    struct Hasher;

    template <>
    struct Hasher<uint8> {
        usize operator()(uint8 value) const {
            return std::hash<uint8>{}(value);
        }
    };

    template <>
    struct Hasher<uint32> {
        usize operator()(uint32 value) const {
            return std::hash<uint32>{}(value);
        }
    };

    template <>
    struct Hasher<unsigned __int64> {
        static unsigned __int64 splitmix64(unsigned __int64 x) {
            x += 0x9e3779b97f4a7c15;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
            x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
            return x ^ (x >> 31);
        }

        usize operator()(unsigned __int64 value) const {
            static const unsigned __int64 FIXED_RANDOM = std::chrono::steady_clock::now().time_since_epoch().count();
            return splitmix64(value + FIXED_RANDOM);
        }
    };

    template <typename T1, typename T2>
    struct Hasher<std::pair<T1, T2>> {
        usize operator()(std::pair<T1, T2> const& value) const {
            auto h1 = std::hash<T1>{}(value.first);
            auto h2 = std::hash<T2>{}(value.second);

            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    template <typename T>
    struct Hasher<godot::Ref<T>> {
        usize operator()(godot::Ref<T> const& value) const { return usize(*value); }
    };

    template <typename T>
    struct Hasher<std::vector<T>> {
        usize operator()(std::vector<T> const& value) const {
            usize hash = 0;
            for (const auto& elem : value) {
                hash ^= Hasher<T>{}(elem) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };

    template <typename T>
    concept Hashable = requires(T t) {
        Hasher<T>{}(t);
    };
}