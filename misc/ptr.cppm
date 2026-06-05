module;

#include <includes.hpp>
#include <atomic>

export module misc.ptr;

import misc.number;
import misc.format;

export namespace craftbuild {
	template <typename T>
	class Ptr {
		T* __value__;
		std::atomic<size>* __rc__ = nullptr;

		inline none init() {
			if (__value__ == nullptr) return;
			__rc__ = new std::atomic<size>(1);
		}

		inline none retain() noexcept {
			if (__rc__) {
				__rc__->fetch_add(1, std::memory_order_relaxed);
			}
		}

	public:
		Ptr() : __value__(nullptr), __rc__(nullptr) {}
		Ptr(T* x) : __value__(x) { init(); }
		Ptr(const Ptr<T>& x) : __value__(x.__value__), __rc__(x.__rc__) { retain(); }
		Ptr(Ptr<T>&& x) noexcept : __value__(x.__value__), __rc__(x.__rc__) {
			x.__value__ = nullptr;
			x.__rc__ = nullptr;
		}
		~Ptr() { clear(); }

		Ptr<T>& operator=(T* x) {
			clear();

			__value__ = x;
			init();

			return *this;
		}
		Ptr<T>& operator=(const Ptr<T>& x) {
			if (this == &x) return *this;

			clear();

			__value__ = x.__value__;
			__rc__ = x.__rc__;
			retain();

			return *this;
		}
		Ptr<T>& operator=(Ptr<T>&& x) noexcept {
			if (this == &x) return *this;

			clear();
			__value__ = x.__value__;
			__rc__ = x.__rc__;

			x.__value__ = nullptr;
			x.__rc__ = nullptr;

			return *this;
		}

		explicit operator bool() const { return __value__ != nullptr; }

		bool operator==(const Ptr<T>& other) const {
			return __value__ == other.__value__;
		}

		none clear() {
			if (not __rc__) return;

			if (__rc__->fetch_sub(1, std::memory_order_acq_rel) == 1) {
				delete __value__;
				delete __rc__;
			}

			__value__ = nullptr;
			__rc__ = nullptr;
		}

		template <typename V = T>
		inline V& value() const {
			if (__value__) return *__value__;
			throw std::runtime_error("Cannot access nullptr of ptr");
		}
		template <typename V = T>
		inline V& value() {
			if (__value__) return *__value__;
			throw std::runtime_error("Cannot access nullptr of ptr");
		}

		inline std::size_t get_count() const noexcept {
			return __rc__ ? __rc__->load(std::memory_order_relaxed) : 0;
		}

		inline std::string address() const {
			return format{} << __value__;
		}

		inline T* unref() {
			if (not __rc__) return nullptr;

			if (__rc__->load(std::memory_order_acquire) != 1)
				throw std::runtime_error("Cannot unref shared ptr");

			T* tmp = __value__;

			delete __rc__;

			__value__ = nullptr;
			__rc__ = nullptr;

			return tmp;
		}

		// DANGER ZONE
		inline T* c_ptr() const noexcept {
			return __value__;
		}

		friend format&& operator<<(format&& fm, const Ptr<T>& d) {
			std::move(fm) << d.value();
			return fm;
		}
	};
}