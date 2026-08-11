export module misc.set;

import std;
import misc.hasher;

export namespace craftbuild {
    template<typename T>
    requires Hashable<T>
    using Set = std::unordered_set<T, Hasher<T>>;

    template<class T, class H, class E, class A>
    bool operator==(std::unordered_set<T, H, E, A>& a, std::unordered_set<T, H, E, A> const& b) {
        if (a.size() != b.size()) return false;

        for (auto const& k : a) if (b.find(k) == b.end()) return false;
        return true;
    }
}