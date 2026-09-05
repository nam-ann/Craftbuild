export module misc.list;

import std;

import misc.str;
import misc.range;
import misc.number;
import misc.hasher;

export namespace craftbuild {
    template <typename T>
    class List {
        inline static std::allocator<T> allocator;

        T* __value__ = nullptr;
        usize __len__ = 0;
        usize __space__ = 0;

    public:
        List() noexcept {}
        List(std::initializer_list<T> const& l) : __space__(l.size()), __len__(l.size()) {
            if (not __space__) return;

            __value__ = allocator.allocate(__space__);
            std::ranges::uninitialized_copy(l, *this);
        }
        List(List const& s) : __len__(s.__len__), __space__(s.__len__) {
            if (not __space__) return;

            __value__ = allocator.allocate(__space__);
            std::ranges::uninitialized_copy(s, *this);
        }
        List(List&& s) noexcept : __value__(nullptr), __len__(0), __space__(0) {
            std::swap(__value__, s.__value__);
            std::swap(__len__, s.__len__);
            std::swap(__space__, s.__space__);
        }

        ~List() {
            std::ranges::destroy_n(__value__, __len__);
            allocator.deallocate(__value__, __space__);
        }

        List& operator=(std::initializer_list<T> const& l) {
            return *this = List(l);
        }
        List& operator=(List const& s) {
            if (this == &s) return *this;

            expect(s.__len__);
            std::ranges::destroy_n(__value__, __len__);
            std::ranges::uninitialized_copy(s, *this);
            __len__ = s.__len__;

            return *this;
        }
        List& operator=(List&& s) noexcept {
            if (this == &s) return *this;

            reset();
            std::swap(__value__, s.__value__);
            std::swap(__len__, s.__len__);
            std::swap(__space__, s.__space__);

            return *this;
        }

        List& operator+=(std::initializer_list<T> const& l) {
            expect(l.size());
            std::ranges::uninitialized_copy(l, end(), end() + l.size());
            __len__ += l.size();

            return *this;
        }
        List& operator+=(T const& t) {
            if (__len__ >= __space__) expect(__len__);

            std::construct_at(__value__ + __len__, t);
            ++__len__;

            return *this;
        }
        List& operator+=(List const& s) {
            if (this == &s) return *this += List(s);

            expect(s.__len__);
            std::ranges::uninitialized_copy(s.begin(), s.end(), end(), end() + s.__len__);
            __len__ += s.__len__;

            return *this;
        }

        List& operator*=(usize n) {
            if (n == 0) {
                reset();
                return *this;
            }

            List const original(*this);
            expect(__len__ * (n - 1));

            for (auto i : range<usize>(n - 1)) {
                std::ranges::uninitialized_copy(
                    original.begin(),
                    original.end(),
                    end(),
                    end() + original.__len__
                );
                __len__ += original.__len__;
            }

            return *this;
        }

        List operator+(std::initializer_list<T> const& s) const { List cache(*this); cache += s; return cache; }
        List operator+(List const& s) const { List cache(*this); cache += s; return cache; }

        List operator*(usize n) const { List cache(*this); return cache *= n; }

        T& operator[](int64 index) {
            if (index < 0) index += __len__;
            if (index < 0 or index >= int64(__len__)) throw std::out_of_range("List index out of range");

            return __value__[index];
        }

		T const& operator[](int64 index) const {
            if (index < 0) index += __len__;
            if (index < 0 or index >= int64(__len__)) throw std::out_of_range("List index out of range");
			
            return __value__[index];
		}

        List operator[](int64 start, int64 end, int64 step) const {
            if (step == 0) throw std::invalid_argument("List slice step cannot be zero");

            int64 const n = int64(__len__);
            auto normalize = [n](int64 index) -> int64 {
                if (index < 0) index += n;
                return index;
            };

            start = normalize(start);
            end = normalize(end);
            List result;

            if (step > 0) {
                start = std::clamp<int64>(start, 0, n);
                end = std::clamp<int64>(end, 0, n);
                
                if (start >= end) return result;
                usize count = usize((end - start + step - 1) / step);
                result.reserve(count);

                for (int64 i : range(start, end, step)) {
                    std::construct_at(result.__value__ + result.__len__++, __value__[i]);
                }
            }
            else {
                start = std::clamp<int64>(start, -1, n - 1);
                end = std::clamp<int64>(end, -1, n - 1);
                
                if (start <= end) return result;
                
                int64 const abs_step = -step;
                usize count = usize((start - end + abs_step - 1) / abs_step);
                result.reserve(count);
                
                for (int64 i : range(start, end, step)) {
                    std::construct_at(result.__value__ + result.__len__++, __value__[i]);
                }
            }
            
            return result;
        }

        operator bool() const {
            return __len__ != 0;
        }

        bool operator==(List const& s) const {
            if (__len__ != s.__len__) return false;

            if (not __value__ and not s.__value__) return true;
            if (not __value__ or not s.__value__) return false;
            if (__value__ == s.__value__) return true;

            return std::ranges::equal(s, *this);
        }

        List& emplace(auto&&... args) {
            if (__len__ >= __space__) expect(__len__);
            std::construct_at(__value__ + __len__, std::forward<decltype(args)>(args)...);
            ++__len__;

            return *this;
        }

        List& append(T const& t) { return *this += t; }
        List& append(List const& l) { return *this += l; }
        List& insert(int64 index, T const& t) {
            if (index < 0) index += int64(__len__);
            if (index < 0 or index > __len__) [[unlikely]] throw std::out_of_range("List index out of range");

            if (__len__ >= __space__) expect(__len__);

            if (index == int64(__len__)) {
                std::construct_at(__value__ + __len__, t);
            }
            else {
                std::construct_at(__value__ + __len__, std::move(__value__[__len__ - 1]));

                std::ranges::move_backward(
                    __value__ + index,
                    __value__ + __len__ - 1,
                    __value__ + __len__
                );

                __value__[index] = t;
            }

            ++__len__;
            return *this;
        }
        List& insert(int64 index, List const& l) {
            if (l.__len__ == 0) return *this;
            if (index < 0) index += int64(__len__);
            if (index < 0 or index > int64(__len__)) [[unlikely]] throw std::out_of_range("List index out of range");

            if (this == &l) return insert(index, List(*this));

            usize const count = l.__len__;
            usize const old_len = __len__;
            usize const tail = old_len - usize(index);

            expect(count);

            if (count <= tail) {
                std::ranges::uninitialized_move(
                    __value__ + old_len - count,
                    __value__ + old_len,
                    __value__ + old_len,
                    __value__ + old_len + count
                );
                std::ranges::move_backward(
                    __value__ + index,
                    __value__ + old_len - count,
                    __value__ + old_len
                );
                std::ranges::copy(
                    l.__value__,
                    l.__value__ + count,
                    __value__ + index
                );
            }
            else {
                std::ranges::uninitialized_move(
                    __value__ + index,
                    __value__ + old_len,
                    __value__ + index + count,
                    __value__ + old_len + count
                );
                std::ranges::copy(
                    l.__value__,
                    l.__value__ + tail,
                    __value__ + index
                );
                std::ranges::uninitialized_copy(
                    l.__value__ + tail,
                    l.__value__ + count,
                    __value__ + old_len,
                    __value__ + old_len + (count - tail)
                );
            }
            
            __len__ += count;
            return *this;
        }

		List& pop(int64 index = -1) {
			if (__len__ == 0) throw std::out_of_range("List is empty");
			if (index < 0) index += __len__;
			if (index < 0 or index >= int64(__len__)) throw std::out_of_range("List index out of range");
			
            if (index + 1 < int64(__len__)) {
                std::ranges::move(__value__ + index + 1, end(), __value__ + index);
            }

            std::destroy_at(__value__ + __len__ - 1);

			--__len__;
			return *this;
		}

        void clear() {
            std::ranges::destroy_n(__value__, __len__);
            __len__ = 0;
        }

        void reset() {
            std::ranges::destroy_n(__value__, __len__);
            allocator.deallocate(__value__, __space__);

            __len__ = __space__ = 0;
        }

        void expect(usize extra) {
            usize needed = __len__ + extra;
            if (not needed) needed = 8;
            reserve(needed);
        }

        void reserve(usize capacity) {
            if (__space__ >= capacity) return;

            T* cache = allocator.allocate(capacity);
            std::ranges::uninitialized_move(__value__, end(), cache, cache + __len__);

            std::ranges::destroy_n(__value__, __len__);
            allocator.deallocate(__value__, __space__);

            __space__ = capacity;
            __value__ = cache;
        }

        void resize(usize new_len, T const& fill_value = T{}) {
            if (new_len < __len__) {
                std::ranges::destroy_n(__value__ + new_len, __len__ - new_len);
            }
            else if (new_len > __len__) {
                usize const extra = new_len - __len__;

                expect(extra);
                std::ranges::uninitialized_fill_n(__value__ + __len__, extra, fill_value);
            }

            __len__ = new_len;
        }

        void fill(T const& fill_value) {
			std::ranges::fill(*this, fill_value);
        }

        void swap(List& other) noexcept {
            if (this == &other) return;

            std::swap(__value__, other.__value__);
            std::swap(__len__, other.__len__);
            std::swap(__space__, other.__space__);
        }

        Str str() const {
            Str result = "[";
            for (auto i : range<usize>(__len__)) {
                result += Str(__value__[i]);
                if (i != __len__ - 1) result += ", ";
            }
            return result += "]";
        }

        T* data() { return __value__; }
        T const* data() const { return __value__; }

        auto begin() noexcept { return __value__; }
        auto end() noexcept { return __value__ + __len__; }
        auto begin() const noexcept { return __value__; }
        auto end() const noexcept { return __value__ + __len__; }

        friend usize len(List const& s) { return s.__len__; }
        friend List operator+(std::initializer_list<T>& l, List const& s) { return List(l) + s; }
    };

    template <typename T>
    struct Hasher<List<T>> {
        usize operator()(List<T> const& value) const {
            usize hash = 0;
            for (const auto& elem : value) {
                hash ^= Hasher<T>{}(elem)+0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };
}