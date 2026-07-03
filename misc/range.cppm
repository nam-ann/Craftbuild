module;

#include <includes.hpp>

export module misc.range;

template <typename T>
requires std::is_arithmetic_v<T>
class Range {
private:
    T __start__, __stop__, __step__;

public:
    Range(T start, T stop, T step = 0) : __start__(start), __stop__(stop), __step__(step == 0 ? (start <= stop ? 1 : -1) : step) {}

    struct Sentinel {
        T stop;
    };

    struct Iterator {
        T value;
        T step;

        bool operator!=(Sentinel s) const {
            return step > 0 ? value < s.stop : value > s.stop;
        }

        T operator*() const { return value; }
        Iterator& operator++() { value += step; return *this; }
    };

    Iterator begin() const { return { __start__, __step__ }; }
    Sentinel end() const { return { __stop__ }; }
};

export namespace craftbuild {
    template <typename T>
    requires std::is_arithmetic_v<T>
    inline Range<T> range(T stop) { return { 0, stop }; }

    template <typename T>
    requires std::is_arithmetic_v<T>
    inline Range<T> range(T start, T stop, T step = 0) { return { start, stop, step }; }
}