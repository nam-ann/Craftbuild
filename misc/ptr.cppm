export module misc.ptr;

import std;

import misc.gc;
import misc.list;
import misc.number;
import misc.format;

export namespace craftbuild {
	template <typename T>
	concept Traceable = requires(T t) { t._get_refs(std::declval<List<GCObject*>&>()); };

	template <typename T>
	struct Obj : GCObject {
		template <typename... Args>
		Obj(Args&&... args) {
			__data__ = new T(std::forward<Args>(args)...);
			GarbageCollector::register_object(this);
		}

		~Obj() noexcept override { delete static_cast<T*>(__data__); }

		void get_refs(List<GCObject*>& refs) noexcept override {
			if constexpr (Traceable<T>) static_cast<T*>(__data__)->_get_refs(refs);
		}
	};

	template <typename T>
	class Ptr {
		GCObject* __value__ = nullptr;

		inline void init() { GarbageCollector::add_root(__value__); }

	public:
		Ptr() noexcept : __value__(nullptr) {}
		Ptr(std::nullptr_t) noexcept : __value__(nullptr) {}

		template <typename U>
		requires std::convertible_to<U*, T*>
		Ptr(Obj<U>* x) : __value__(x) { init(); }

		template <typename U>
		requires std::convertible_to<U*, T*>
		Ptr(Ptr<U> const& x) : __value__(x.__value__) { init(); }

		template <typename U>
		requires std::convertible_to<U*, T*>
		Ptr(Ptr<U>&& x) noexcept : __value__(x.__value__) { x.__value__ = nullptr; init(); }

		~Ptr() { clear(); }

		template <typename U>
		requires std::convertible_to<U*, T*>
		Ptr<T>& operator=(Obj<U>* x) {
			clear();
			__value__ = x;
			init();
			return *this;
		}

		template <typename U>
		requires std::convertible_to<U*, T*>
		Ptr<T>& operator=(Ptr<U> const& x) {
			if (__value__ == x.__value__) [[unlikely]] return *this;
			clear();
			__value__ = x.__value__;
			init();
			return *this;
		}

		template <typename U>
		requires std::convertible_to<U*, T*>
		Ptr<T>& operator=(Ptr<U>&& x) noexcept {
			if (__value__ == x.__value__) [[unlikely]] return *this;
			clear();
			__value__ = x.__value__;
			x.__value__ = nullptr;
			init();
			return *this;
		}

		explicit operator bool() const { return __value__ != nullptr; }

		bool operator==(Ptr const& other) const { return __value__ == other.__value__; }

		void clear() {
			if (not __value__) [[unlikely]] return;
			GarbageCollector::remove_root(__value__);
			__value__ = nullptr;
		}

		void swap(Ptr& other) noexcept {
			auto* cache = __value__;

			__value__ = other.__value__;
			other.__value__ = cache;
		}

		inline Obj<T>* object() const noexcept { return static_cast<Obj<T>*>(__value__); }

		inline T& value() const {
			if (__value__) [[likely]] return *(T*)__value__->__data__;
			throw std::runtime_error("Cannot access nullptr of ptr");
		}
		inline T& value() {
			if (__value__) [[likely]] return *(T*)__value__->__data__;
			throw std::runtime_error("Cannot access nullptr of ptr");
		}

		inline Str address() const {
			return ""f << __value__;
		}

		inline T* data() const noexcept {
			return __value__ ? static_cast<T*>(__value__->__data__) : nullptr;
		}

		friend format&& operator<<(format&& fm, Ptr<T> const& d) {
			std::move(fm) << d.value();
			return fm;
		}

		template <typename> friend class Ptr;
	};
}