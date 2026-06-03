module;

#include <includes.hpp>
#include <xhash>

export module misc.hasher;

import misc.number;

export namespace craftbuild {
    template <typename T>
    struct Hasher;

    template <>
    struct Hasher<uint8> {
        size operator()(uint8 value) const {
            return std::hash<uint8>{}(value);
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

        size operator()(unsigned __int64 value) const {
            static const unsigned __int64 FIXED_RANDOM = std::chrono::steady_clock::now().time_since_epoch().count();
            return splitmix64(value + FIXED_RANDOM);
        }
    };

    template <typename T>
    concept Hashable = requires(T t) {
        Hasher<T>{}(t);
    };
}