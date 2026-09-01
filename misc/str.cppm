export module misc.str;

import std;

import misc.range;
import misc.number;
import misc.hasher;

inline std::u32string ptr_to_hex(void const* ptr) noexcept {
    std::uintptr_t value = (std::uintptr_t)ptr;

    if (value == 0) return U"0x0";

    byte32 buffer[2 + sizeof(std::uintptr_t) * 2 + 1]; // "0x" + hex + null
    int32 i = sizeof(buffer) - 1;
    buffer[i--] = U'\0';

    byte32 const* hex = U"0123456789ABCDEF";

    while (value > 0) {
        buffer[i--] = hex[value & 0xF];
        value >>= 4;
    }

    buffer[i--] = U'x';
    buffer[i] = U'0';

    return &buffer[i];
}

inline std::u32string to_u32(std::string_view s) {
    std::u32string out;
    usize i = 0;

    while (i < s.size()) {
        uint32 cp = 0;
        unsigned char c = s[i];

        if (c < 0x80) { // 1 char
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
inline void append_utf8(std::string& out, uint32 cp) {
    // basic validation
    if (cp > 0x10FFFF or (cp >= 0xD800 && cp <= 0xDFFF)) {
        out += "?";
        return;
    }

    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

export namespace craftbuild {
    class Str {
        uint8* __value__ = nullptr;
        usize __len__ = 0;
        usize __space__ = 0;

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
            bool operator!=(Iterator const& other) const {
                return __ptr__ != other.__ptr__;
            }
        };

        void append(uint8 b) {
            if (__len__ >= __space__) expect(__len__);
            __value__[__len__++] = b;
        }

        // ENCODE: codepoint -> UEF-8
        void encode(uint32 cp) {
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
            append(chunks[0]); // last char
        }

        // encode UTF-32 string
        void encode(std::u32string const& s) {
            expect(s.size() * 2);
            for (byte32 cp : s) {
                encode((uint32)cp);
            }
        }

        // DECODE: UEF-8 -> codepoint
        uint32 decode_one(size_t& i) const {
            uint32 result = 0;

            while (i < __len__) {
                uint8 b = __value__[i++];
                result = (result << 7) | (b & 0x7F);

                if ((b & 0x80) == 0) break;
            }

            return result;
        }

    public:
        Str() noexcept {}
        explicit Str(int64 i) { encode(to_u32(std::to_string(i))); }
        explicit Str(int32 i) { encode(to_u32(std::to_string(i))); }
        explicit Str(uint64 i) { encode(to_u32(std::to_string(i))); }
        explicit Str(uint32 i) { encode(to_u32(std::to_string(i))); }
        explicit Str(float64 i) { encode(to_u32(std::to_string(i))); }
        explicit Str(void const* v) { encode(ptr_to_hex(v)); }
        Str(byte32 const* c) { encode(c); }
        Str(char const* s) { encode(to_u32(s)); }
        Str(std::u32string const& s) { encode(s); }
        Str(std::string const& s) { encode(to_u32(s)); }
        Str(std::string_view s) { encode(to_u32(s)); }
        Str(Str const& s) : __value__(new uint8[s.__len__]), __len__(s.__len__), __space__(s.__len__) { std::memcpy(__value__, s.__value__, __len__); }
        Str(Str&& s) noexcept : __value__(s.__value__), __len__(s.__len__), __space__(s.__space__) {
            s.__value__ = nullptr;
            s.__len__ = s.__space__ = 0;
        }

        ~Str() { clear(); }

        Str& operator=(byte32 const* c) { *this = std::u32string(c); return *this; }
        Str& operator=(char const* c) { *this = to_u32(c); return *this; }
        Str& operator=(std::u32string const& s) {
            clear();
            encode(s);
            return *this;
        }
        Str& operator=(std::string const& s) { *this = to_u32(s); return *this; }
        Str& operator=(std::string_view s) { *this = to_u32(s); return *this; }
        Str& operator=(Str const& s) {
            if (this == &s) return *this;
            clear();
            __value__ = new uint8[s.__len__];
            std::memcpy(__value__, s.__value__, s.__len__);
            __len__ = __space__ = s.__len__;
            return *this;
        }
        Str& operator=(Str&& s) noexcept {
            if (this == &s) return *this;
            clear();
            __value__ = s.__value__;
            __len__ = s.__len__;
            __space__ = s.__space__;

            s.__value__ = nullptr;
            s.__len__ = s.__space__ = 0;

            return *this;
        }

        Str& operator+=(byte32 const* c) { *this += std::u32string(c); return *this; }
        Str& operator+=(char const* c) { *this += std::string_view(c); return *this; }
        Str& operator+=(std::u32string const& s) { encode(s); return *this; }
        Str& operator+=(std::string const& s) { encode(to_u32(s)); return *this; }
        Str& operator+=(std::string_view s) { encode(to_u32(s)); return *this; }
        Str& operator+=(Str const& s) {
            expect(s.__len__);
            std::memcpy(&__value__[__len__], s.__value__, s.__len__);
            __len__ += s.__len__;
            return *this;
        }
        Str& operator+=(Str&& s) noexcept {
            if (this == &s) return *this;
            expect(s.__len__);
            std::memcpy(&__value__[__len__], s.__value__, s.__len__);
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
                std::memcpy(&__value__[__len__], original.__value__, original.__len__);
                __len__ += original.__len__;
            }
            return *this;
        }

        Str operator+(byte32 const* c) const { Str cache(*this); return cache += c; }
        Str operator+(char const* c) const { Str cache(*this); return cache += c; }
        Str operator+(std::u32string const& s) const { Str cache(*this); cache += s; return cache; }
        Str operator+(std::string const& s) const { Str cache(*this); cache += s; return cache; }
        Str operator+(std::string_view s) const { Str cache(*this); cache += s; return cache; }
        Str operator+(Str const& s) const { Str cache(*this); cache += s; return cache; }
        Str operator+(Str&& s) const noexcept { Str cache(*this); cache += s; return cache; }

        Str operator*(usize n) const { Str cache(*this); return cache *= n; }

        uint8& operator[](usize pos) { return __value__[pos]; }
        uint8 const& operator[](usize pos) const { return __value__[pos]; }

        operator bool() const { return *this != U""; }

        bool operator==(Str const& s) const {
            return __len__ == s.__len__ and std::memcmp(__value__, s.__value__, __len__) == 0;
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
                std::memcpy(cache, __value__, __len__);
                delete[] __value__;
            }

            __space__ = extra;
            __value__ = cache;
        }

        void resize(usize new_len, byte32 fill_value = U' ') {
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

        void swap(Str& other) noexcept {
            if (this == &other) return;

            std::swap(__value__, other.__value__);
            std::swap(__len__, other.__len__);
            std::swap(__space__, other.__space__);
        }

        uint8* c_ptr() { return __value__; }
        uint8 const* c_ptr() const { return __value__; }

        Iterator begin() { return Iterator(__value__); }
        Iterator end() { return Iterator(__value__ + __len__); }
        Iterator begin() const { return Iterator(__value__); }
        Iterator end() const { return Iterator(__value__ + __len__); }

        friend std::ostream& operator<<(std::ostream& os, Str const& s) noexcept {
            return os << s.std_str();
        }

        friend usize len(Str const& s) { return s.__len__; }

        friend Str operator+(byte32 const* c, Str const& s) { return Str(c) + s; }
        friend Str operator+(char const* c, Str const& s) { return Str(c) + s; }
        friend Str operator+(std::string const& std_s, Str const& s) { return Str(std_s) + s; }
        friend Str operator+(std::string_view std_s, Str const& s) { return Str(std_s) + s; }
        friend Str operator+(std::u32string const& std_s, Str const& s) { return Str(std_s) + s; }

        friend struct Hasher<Str>;
    };

    template <>
    struct Hasher<Str> {
        usize operator()(Str const& str) const {
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