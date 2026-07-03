module;

#include <includes.hpp>
#include <unordered_set>

export module misc.set;

import misc.hasher;

export namespace craftbuild {
    template<typename T>
    requires Hashable<T>
    using Set = std::unordered_set<T, Hasher<T>>;

    template<class T, class H, class E, class A>
    bool operator==(const std::unordered_set<T, H, E, A>& a, const std::unordered_set<T, H, E, A>& b) {
        if (a.size() != b.size()) return false;

        for (auto const& k : a) if (b.find(k) == b.end()) return false;
        return true;
    }
}