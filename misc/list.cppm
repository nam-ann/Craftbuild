module;

#include <limits>
#include <iostream>
#include <utility>
#include <cstring>
#include <compare>
#include <ratio>
#include <cstddef>
#include <charconv>
#include <memory>
#include <stdexcept>
#include <initializer_list>
#include <concepts>

export module misc.list;

import misc.str;
import misc.range;
import misc.number;
import misc.hasher;

export namespace craftbuild {
    template <typename T>
    requires std::is_trivially_copyable_v<T>
    class List {
        T* __value__;
        size __len__;
        size __space__;

        struct Iterator {
            T* __ptr__;

            Iterator(T* p) : __ptr__(p) {}

            T& operator*() {
                return *__ptr__;
            }
            Iterator& operator++() {
                __ptr__++;
                return *this;
            }
            bool operator!=(const Iterator& other) const {
                return __ptr__ != other.__ptr__;
            }
        };

    public:
        List() : __value__(nullptr), __len__(0), __space__(0) {}
        List(const std::initializer_list<T>& l) : __value__(new T[l.size()]), __space__(l.size()), __len__(l.size()) { memcpy(__value__, l.data(), __len__ * sizeof(T)); }
        List(const List& s) : __value__(new T[s.__len__]), __len__(s.__len__), __space__(s.__len__) { memcpy(__value__, s.__value__, __len__ * sizeof(T)); }
        List(List&& s) noexcept : __value__(nullptr), __len__(0), __space__(0) {
            std::swap(__value__, s.__value__);
            std::swap(__len__, s.__len__);
            std::swap(__space__, s.__space__);
        }

        ~List() { clear(); }

        List& operator=(const std::initializer_list<T>& l) {
            return *this = List(l);
        }
        List& operator=(const List& s) {
            if (this == &s) return *this;
            clear();
            __value__ = new T[s.__len__];
            memcpy(__value__, s.__value__, s.__len__ * sizeof(T));
            __len__ = __space__ = s.__len__;
            return *this;
        }
        List& operator=(List&& s) noexcept {
            if (this == &s) return *this;
            clear();
            std::swap(__value__, s.__value__);
            std::swap(__len__, s.__len__);
            std::swap(__space__, s.__space__);
            return *this;
        }

        List& operator+=(const std::initializer_list<T>& l) {
            expect(l.size());
            memcpy(&__value__[__len__], l.data(), l.size() * sizeof(T));
            __len__ += l.size();
            return *this;
        }
        List& operator+=(const T& t) {
            if (__len__ >= __space__) expect(__len__);
            __value__[__len__++] = t;
            return *this;
        }
        List& operator+=(const List& s) {
            if (this == &s) return *this += List(s);
            expect(s.__len__);
            memcpy(&__value__[__len__], s.__value__, s.__len__ * sizeof(T));
            __len__ += s.__len__;
            return *this;
        }
        List& operator+=(List&& s) noexcept {
            if (this == &s) return *this;
            expect(s.__len__);
            memcpy(&__value__[__len__], s.__value__, s.__len__ * sizeof(T));
            __len__ += s.__len__;
            s.clear();
            return *this;
        }

        List& operator*=(size n) {
            if (n == 0) {
                clear();
                return *this;
            }
            const List original(*this);
            expect(__len__ * (n - 1));
            for (auto i : range<size>(n - 1)) {
                memcpy(&__value__[__len__], original.__value__, original.__len__ * sizeof(T));
                __len__ += original.__len__;
            }
            return *this;
        }

        List operator+(const std::initializer_list<T>& s) const { List cache(*this); cache += s; return cache; }
        List operator+(const List& s) const { List cache(*this); cache += s; return cache; }
        List operator+(List&& s) const noexcept { List cache(*this); cache += s; return cache; }

        List operator*(size n) const { List cache(*this); return cache *= n; }

        T& operator[](size pos) {
            if (pos >= __len__) throw std::out_of_range("List index out of range");
            return __value__[pos];
        }

        operator bool() const {
            return __len__ != 0;
        }

        bool operator==(const List& s) const {
            if (__len__ != s.__len__) return false;

            if (not __value__ and not s.__value__) return true;
            if (not __value__ or not s.__value__) return false;
            if (__value__ == s.__value__) return true;

            return memcmp(__value__, s.__value__, __len__ * sizeof(T)) == 0;
        }

        List& append(const T& t) {
            return *this += t;
        }
        List& insert(size index, const T& t) {
            if (index < __len__) __value__[index] = t;
            return *this;
        }

        none clear() {
            delete[] __value__;
            __value__ = nullptr;
            __len__ = __space__ = 0;
        }

        none expect(size extra) {
            size needed = __len__ + extra;
            if (not needed) needed = 8;
            archive(needed);
        }

        none archive(size extra) {
            if (__space__ >= extra) return;
            __space__ = extra;

            T* cache = new T[extra];
            if (__value__) {
                memcpy(cache, __value__, __len__ * sizeof(T));
                delete[] __value__;
            }

            __value__ = cache;
            cache = nullptr;
        }

        none resize(size new_len, const T& fill_value = T{}) {
            if (new_len > __space__) archive(new_len);
            if (new_len > __len__) for (auto i : range<size>(__len__, new_len)) __value__[i] = fill_value;
            __len__ = new_len;
        }

        none fill(const T& fill_value) {
            for (auto i : range<size>(__len__)) __value__[i] = fill_value;
        }

        none swap(List& other) noexcept {
            if (this == &other) return;

            std::swap(__value__, other.__value__);
            std::swap(__len__, other.__len__);
            std::swap(__space__, other.__space__);
        }

        Str str() const {
            Str result = "[";
            for (auto i : range<size>(__len__)) {
                result += Str(__value__[i]);
                if (i != __len__ - 1) result += ", ";
            }
            return result += "]";
        }

        T* c_ptr() {
            return __value__;
        }
        const T* c_ptr() const {
            return __value__;
        }

        Iterator begin() { return Iterator(__value__); }
        Iterator end() { return Iterator(__value__ + __len__); }
        Iterator begin() const { return Iterator(__value__); }
        Iterator end() const { return Iterator(__value__ + __len__); }

        friend size len(const List& s) { return s.__len__; }
        friend List operator+(const std::initializer_list<T>& l, const List& s) { return List(l) + s; }
    };
}