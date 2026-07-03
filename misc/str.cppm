module;

#include <limits>
#include <string>
#include <vector>
#include <iostream>
#include <utility>
#include <cstring>
#include <format>
#include <compare>
#include <ratio>
#include <cstddef>
#include <charconv>
#include <memory>
#include <stdexcept>

export module misc.str;

import misc.range;
import misc.number;
import misc.hasher;

inline std::u32string ptr_to_hex(const none* ptr) noexcept {
    uintptr_t value = (uintptr_t)ptr;

    if (value == 0) return U"0x0";

    byte32 buffer[2 + sizeof(uintptr_t) * 2 + 1]; // "0x" + hex + null
    int32 i = sizeof(buffer) - 1;
    buffer[i--] = U'\0';

    const byte32* hex = U"0123456789ABCDEF";

    while (value > 0) {
        buffer[i--] = hex[value & 0xF];
        value >>= 4;
    }

    buffer[i--] = U'x';
    buffer[i] = U'0';

    return &buffer[i];
}

inline std::u32string to_u32(const std::string& s) {
    std::u32string out;
    usize i = 0;

    while (i < s.size()) {
        uint32 cp = 0;
        unsigned char c = s[i];

        if (c < 0x80) { // 1 byte
            cp = c;
            i += 1;
        }
        else if ((c >> 5) == 0x6) { // 2 bytes
            cp = ((c & 0x1F) << 6) |
                (s[i + 1] & 0x3F);
            i += 2;
        }
        else if ((c >> 4) == 0xE) { // 3 bytes
            cp = ((c & 0x0F) << 12) |
                ((s[i + 1] & 0x3F) << 6) |
                (s[i + 2] & 0x3F);
            i += 3;
        }
        else if ((c >> 3) == 0x1E) { // 4 bytes
            cp = ((c & 0x07) << 18) |
                ((s[i + 1] & 0x3F) << 12) |
                ((s[i + 2] & 0x3F) << 6) |
                (s[i + 3] & 0x3F);
            i += 4;
        }
        else throw std::invalid_argument("invalid UTF-8");

        out.push_back(cp);
    }

    return out;
}

// UTF-8 ENCODE
inline none append_utf8(std::string& out, uint32 cp) {
    // basic validation
    if (cp > 0x10FFFF or (cp >= 0xD800 && cp <= 0xDFFF)) {
        out += "?";
        return;
    }

    if (cp <= 0x7F) {
        out.push_back(static_cast<byte>(cp));
    }
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<byte>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<byte>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF) {
        out.push_back(static_cast<byte>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<byte>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<byte>(0x80 | (cp & 0x3F)));
    }
    else {
        out.push_back(static_cast<byte>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<byte>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<byte>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<byte>(0x80 | (cp & 0x3F)));
    }
}

export namespace craftbuild {
    class Str {
        uint8* __value__;
        usize __len__;
        usize __space__;

        struct Iterator {
            uint8* __ptr__;

            Iterator(uint8* p) : __ptr__(p) {}

            uint8& operator*() {
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

        none append(uint8 byte) {
            if (__len__ >= __space__) expect(__len__);
            __value__[__len__++] = byte;
        }

        // ENCODE: codepoint -> UEF-8
        none encode(uint32 cp) {
            if (cp < 0x80) { // ASCII
                append((uint8)cp);
                return;
            }

            uint8 chunks[5];
            uint8 n = 0;

            while (cp > 0) {
                chunks[n++] = cp & 0x7F;
                cp >>= 7;
            }

            // MSB-first
            for (auto i : range<int32>(n - 1, 0)) {
                append(chunks[i] | 0x80); // continuation
            }
            append(chunks[0]); // last byte
        }

        // encode UTF-32 string
        none encode(const std::u32string& s) {
            expect(s.size() * 2);
            for (byte32 cp : s) {
                encode((uint32)cp);
            }
        }

        // DECODE: UEF-8 -> codepoint
        uint32 decode_one(size_t& i) const {
            uint32 result = 0;

            while (i < __len__) {
                uint8 byte = __value__[i++];
                result = (result << 7) | (byte & 0x7F);

                if ((byte & 0x80) == 0) break;
            }

            return result;
        }

    public:
        Str() : __value__(nullptr), __len__(0), __space__(0) {}
        explicit Str(int64 i) : __value__(nullptr), __len__(0), __space__(0) { encode(to_u32(std::to_string(i))); }
        explicit Str(int32 i) : __value__(nullptr), __len__(0), __space__(0) { encode(to_u32(std::to_string(i))); }
        explicit Str(uint64 i) : __value__(nullptr), __len__(0), __space__(0) { encode(to_u32(std::to_string(i))); }
        explicit Str(uint32 i) : __value__(nullptr), __len__(0), __space__(0) { encode(to_u32(std::to_string(i))); }
        explicit Str(float64 i) : __value__(nullptr), __len__(0), __space__(0) { encode(to_u32(std::to_string(i))); }
        explicit Str(const void* v) : __value__(nullptr), __len__(0), __space__(0) { encode(ptr_to_hex(v)); }
        Str(const byte32* c) : __value__(nullptr), __len__(0), __space__(0) { encode(c); }
        Str(const char* s) : __value__(nullptr), __len__(0), __space__(0) { encode(to_u32(s)); }
        Str(const std::u32string& s) : __value__(nullptr), __len__(0), __space__(0) { encode(s); }
        Str(const std::string& s) : __value__(nullptr), __len__(0), __space__(0) { encode(to_u32(s)); }
        Str(const Str& s) : __value__(new uint8[s.__len__]), __len__(s.__len__), __space__(s.__len__) { memcpy(__value__, s.__value__, __len__); }
        Str(Str&& s) noexcept : __value__(s.__value__), __len__(std::move(s.__len__)), __space__(std::move(s.__space__)) { s.__value__ = nullptr; }

        ~Str() { clear(); }

        Str& operator=(const byte32* c) { *this = std::u32string(c); return *this; }
        Str& operator=(const char* c) { *this = to_u32(c); return *this; }
        Str& operator=(const std::u32string& s) {
            clear();
            encode(s);
            return *this;
        }
        Str& operator=(const std::string& s) { *this = to_u32(s); return *this;  }
        Str& operator=(const Str& s) {
            if (this == &s) return *this;
            clear();
            __value__ = new uint8[s.__len__];
            memcpy(__value__, s.__value__, s.__len__);
            __len__ = __space__ = s.__len__;
            return *this;
        }
        Str& operator=(Str&& s) noexcept {
            if (this == &s) return *this;
            clear();
            __value__ = s.__value__;
            __len__ = std::move(s.__len__);
            __space__ = std::move(s.__space__);
            s.__value__ = nullptr;
            return *this;
        }

        Str& operator+=(const byte32* c) { *this += std::u32string(c); return *this; }
        Str& operator+=(const char* c) { *this += std::string(c); return *this; }
        Str& operator+=(const std::u32string& s) { encode(s); return *this; }
        Str& operator+=(const std::string& s) { encode(to_u32(s)); return *this; }
        Str& operator+=(const Str& s) {
            expect(s.__len__);
            memcpy(&__value__[__len__], s.__value__, s.__len__);
            __len__ += s.__len__;
            return *this;
        }
        Str& operator+=(Str&& s) noexcept {
            if (this == &s) return *this;
            expect(s.__len__);
            memcpy(&__value__[__len__], s.__value__, s.__len__);
            __len__ += s.__len__;
            s.clear();
            return *this;
        }

        Str& operator*=(usize n) {
            if (n == 0) {
                clear();
                return *this;
            }
            Str original(*this);
            expect(__len__ * (n - 1));
            for (auto i : range<usize>(n - 1)) {
                memcpy(&__value__[__len__], original.__value__, original.__len__);
                __len__ += original.__len__;
            }
            return *this;
        }

        Str operator+(const byte32* c) const { Str cache(*this); return cache += c; }
        Str operator+(const char* c) const { Str cache(*this); return cache += c; }
        Str operator+(const std::u32string& s) const { Str cache(*this); cache += s; return cache; }
        Str operator+(const std::string& s) const { Str cache(*this); cache += s; return cache; }
        Str operator+(const Str& s) const { Str cache(*this); cache += s; return cache; }
        Str operator+(Str&& s) const noexcept { Str cache(*this); cache += s; return cache; }

        Str operator*(usize n) const { Str cache(*this); return cache *= n; }

        uint8& operator[](usize pos) { return __value__[pos]; }
        const uint8& operator[](usize pos) const { return __value__[pos]; }

        operator bool() const {
            return *this != U"";
        }

        bool operator==(const Str& s) const {
            return __len__ == s.__len__ and memcmp(__value__, s.__value__, __len__) == 0;
        }

        // FULL DECODE -> UTF-8 string
        std::string std_str() const {
            std::string out;
            size_t i = 0;

            while (i < __len__) {
                uint32 cp = decode_one(i);
                append_utf8(out, cp);
            }

            return out;
        }

        void clear() {
            delete[] __value__;
            __value__ = nullptr;
            __len__ = __space__ = 0;
        }

        void expect(usize extra) {
            usize needed = __len__ + extra;
            if (not needed) needed = 8;
            archive(needed);
        }

        void archive(usize extra) {
            if (__space__ >= extra) return;

            uint8* cache = new uint8[extra];
            if (__value__) {
                memcpy(cache, __value__, __len__);
                delete[] __value__;
            }

            __space__ = extra;
            __value__ = cache;
        }

        none resize(usize new_len, byte32 fill_value = U' ') {
            if (new_len > __space__) archive(new_len);
            if (new_len > __len__) for (auto i : range<usize>(__len__, new_len)) encode(fill_value);
            __len__ = new_len;
        }

        usize& sync_pos(usize& pos) const {
            if (pos >= __len__) throw std::runtime_error(std::format("you accessed using index {} while the length was {}", pos, __len__));
            while (pos > 0 and (__value__[pos] & 0x80) != 0) --pos;
            return ++pos;
        }
        usize get_sync_pos(usize pos) const {
            return sync_pos(pos);
        }

        usize& next_pos(usize& pos) const {
            if (pos >= __len__) throw std::runtime_error(std::format("you accessed using index {} while the length was {}", pos, __len__));
            while (pos < __len__ and (__value__[pos] & 0x80) != 0) ++pos;
            return ++pos;
        }
        usize get_next_pos(usize pos) const {
            return next_pos(pos);
        }

        uint32 sync(usize& pos) const {
            return decode_one(sync_pos(pos));
        }
        uint32 get_sync(usize pos) const {
            return sync(pos);
        }

        Str& add_codepoint(uint32 cp) {
            encode(cp);
            return *this;
		}

        none swap(Str& other) noexcept {
            if (this == &other) return;

            std::swap(__value__, other.__value__);
            std::swap(__len__, other.__len__);
            std::swap(__space__, other.__space__);
        }

        uint8* c_ptr() { return __value__; }
        const uint8* c_ptr() const { return __value__; }

        Iterator begin() { return Iterator(__value__); }
        Iterator end() { return Iterator(__value__ + __len__); }
        Iterator begin() const { return Iterator(__value__); }
        Iterator end() const { return Iterator(__value__ + __len__); }

        friend std::ostream& operator<<(std::ostream& os, const Str& s) noexcept {
            return os << s.std_str();
        }

        friend usize len(const Str& s) { return s.__len__; }

        friend Str operator+(const byte32* c, const Str& s) { return Str(c) + s; }
        friend Str operator+(const char* c, const Str& s) { return Str(c) + s; }
        friend Str operator+(const std::string& std_s, const Str& s) { return Str(std_s) + s; }
        friend Str operator+(const std::u32string& std_s, const Str& s) { return Str(std_s) + s; }

        friend struct Hasher<Str>;
    };

    template <>
    struct Hasher<Str> {
        usize operator()(const Str& str) const {
            constexpr usize FNV_OFFSET = 14695981039346656037ULL;
            constexpr usize FNV_PRIME = 1099511628211ULL;

            usize hash = FNV_OFFSET;

            for (auto i : range<usize>(str.__len__)) {
                hash ^= str.__value__[i];
                hash *= FNV_PRIME;
            }

            hash ^= str.__len__;
            hash *= FNV_PRIME;

            return hash;
        }
    };
}