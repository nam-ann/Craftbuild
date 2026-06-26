module;

#include <includes.hpp>
#include <unordered_map>

export module misc.dict;

import misc.hasher;

export namespace craftbuild {
    template<typename T, typename T2>
    requires Hashable<T>
    using Dict = std::unordered_map<T, T2, Hasher<T>>;

    template<class K, class V, class H, class E, class A>
    bool operator==(const std::unordered_map<K, V, H, E, A>& a, const std::unordered_map<K, V, H, E, A>& b) {
        if (a.size() != b.size()) return false;

        for (auto const& [k, v] : a) {
            auto it = b.find(k);
            if (it == b.end() or not (it->second == v)) return false;
        }
        return true;
    }
}