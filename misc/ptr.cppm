export module misc.ptr;

import std;

import misc.gc;
import misc.str;
import misc.list;
import misc.number;
import misc.format;

export namespace craftbuild {
	template <typename T>
	concept Traceable = requires(T t) { t._get_refs(std::declval<List<GCObject*>&>()); };

	template <typename T>
	struct Obj final : GCObject {
		T __val__;

		template <typename... Args>
		Obj(Args&&... args) : __val__(std::forward<Args>(args)...), GCObject(&__val__) {}

		void get_refs(List<GCObject*>& refs) noexcept override final {
			if constexpr (Traceable<T>) {
				try { __val__._get_refs(refs); }
				catch (...) {} // Ignore
			}
		}

	private:
		~Obj() noexcept override final = default;
	};

	template <typename T>
	class Ptr final {
		GCObject* __value__ = nullptr;

		inline void init() {
			garbage_collector::register_object(__value__);
			garbage_collector::add_root(__value__);
		}

		inline void ref() {
			garbage_collector::add_root(__value__);
		}

		inline void raw_clear() {
			if (not __value__) [[unlikely]] return;
			garbage_collector::remove_root(__value__);
		}

	public:
		Ptr() noexcept {}
		Ptr(std::nullptr_t) noexcept {}

		template <typename U>
		requires std::convertible_to<U*, T*>
		Ptr(Obj<U>* x) : __value__(x) { init(); }

		template <typename U>
		requires std::convertible_to<U*, T*>
		Ptr(Ptr<U> const& x) : __value__(x.__value__) { ref(); }

		template <typename U>
		requires std::convertible_to<U*, T*>
		Ptr(Ptr<U>&& x) noexcept : __value__(x.__value__) { x.__value__ = nullptr; }

		~Ptr() { raw_clear(); }

		template <typename U>
		requires std::convertible_to<U*, T*>
		Ptr<T>& operator=(Obj<U>* x) {
			raw_clear();
			__value__ = x;
			init();
			return *this;
		}

		template <typename U>
		requires std::convertible_to<U*, T*>
		Ptr<T>& operator=(Ptr<U> const& x) {
			if (__value__ == x.__value__) [[unlikely]] return *this;
			raw_clear();
			__value__ = x.__value__;
			ref();
			return *this;
		}

		template <typename U>
		requires std::convertible_to<U*, T*>
		Ptr<T>& operator=(Ptr<U>&& x) noexcept {
			if (__value__ == x.__value__) [[unlikely]] return *this;
			raw_clear();
			__value__ = x.__value__;
			x.__value__ = nullptr;
			return *this;
		}

		explicit operator bool() const { return __value__ != nullptr; }

		bool operator==(Ptr const& other) const { return __value__ == other.__value__; }

		void clear() {
			raw_clear();
			__value__ = nullptr;
		}

		void swap(Ptr& other) noexcept {
			auto* cache = __value__;

			__value__ = other.__value__;
			other.__value__ = cache;
		}

		[[nodiscard]]
		inline GCObject* object() const noexcept { return __value__; }

		inline T& value() const {
			if (__value__) [[likely]] return *reinterpret_cast<T*>(__value__->__data__);
			throw std::runtime_error("Cannot access nullptr of ptr");
		}

		inline Str address() const {
			return ""f << __value__;
		}

		inline T* data() const noexcept {
			return __value__ ? reinterpret_cast<T*>(__value__->__data__) : nullptr;
		}

		friend format&& operator<<(format&& fm, Ptr<T> const& d) {
			std::move(fm) << d.value();
			return fm;
		}

		template <typename> friend class Ptr;
	};
}