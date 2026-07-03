module;

#include <includes.hpp>
#include <atomic>
#include <concepts>
#include <unordered_set>

export module misc.ptr;

import misc.gc;
import misc.number;
import misc.format;
import game.logger;

export namespace craftbuild {
	template <typename T>
	concept Traceable = requires(T t) { t._get_refs(std::declval<std::unordered_set<GCObject*>&>()); };

	template <typename T>
	struct Obj : GCObject {
		template <typename... Args>
		Obj(Args&&... args) {
			__data__ = new T(std::forward<Args>(args)...);
			__deleter__ = [](void* ptr) { delete static_cast<T*>(ptr); };
			if constexpr (Traceable<T>) {
				__get_refs__ = [](void* ptr, std::unordered_set<GCObject*>& refs) {
					T* obj = static_cast<T*>(ptr);
					obj->_get_refs(refs);
				};
			}
			else __get_refs__ = nullptr;
			GarbageCollector::register_object(this);
		}
	};

	template <typename T>
	class Ptr {
		Obj<T>* __value__ = nullptr;

		void init() { GarbageCollector::add_root(__value__); }

	public:
		Ptr() : __value__(nullptr) {}

		template <typename _T>
		requires std::derived_from<_T, T>
		Ptr(Obj<_T>* x) : __value__((Obj<T>*)x) { init(); }
		Ptr(Obj<T>* x) : __value__(x) { init(); }

		template <typename _T>
		requires std::derived_from<_T, T>
		Ptr(const Ptr<_T>& x) : __value__((Obj<T>*)x.__value__) { init(); }
		Ptr(const Ptr<T>& x) : __value__(x.__value__) { init(); }

		template <typename _T>
		requires std::derived_from<_T, T>
		Ptr(Ptr<_T>&& x) noexcept : __value__((Obj<T>*)x.__value__) { x.__value__ = nullptr; init(); }
		Ptr(Ptr<T>&& x) noexcept : __value__(x.__value__) { x.__value__ = nullptr; }
		~Ptr() { clear(); }

		template <typename _T>
		requires std::derived_from<_T, T>
		Ptr<T>& operator=(Obj<_T>* x) {
			clear();
			__value__ = (Obj<T>*)x;
			init();
			return *this;
		}
		Ptr<T>& operator=(Obj<T>* x) {
			clear();
			__value__ = x;
			init();
			return *this;
		}

		template <typename _T>
		requires std::derived_from<_T, T>
		Ptr<T>& operator=(const Ptr<_T>& x) {
			if (__value__ == (Obj<T>*)x.__value__) [[unlikely]] return *this;
			clear();
			__value__ = (Obj<T>*)x.__value__;
			init();
			return *this;
		}
		Ptr<T>& operator=(const Ptr<T>& x) {
			if (__value__ == x.__value__) [[unlikely]] return *this;
			clear();
			__value__ = x.__value__;
			init();
			return *this;
		}

		template <typename _T>
		requires std::derived_from<_T, T>
		Ptr<T>& operator=(Ptr<_T>&& x) noexcept {
			if (__value__ == (Obj<T>*)x.__value__) [[unlikely]] return *this;
			clear();
			__value__ = (Obj<T>*)x.__value__;
			x.__value__ = nullptr;
			init();
			return *this;
		}
		Ptr<T>& operator=(Ptr<T>&& x) noexcept {
			if (__value__ == x.__value__) [[unlikely]] return *this;
			clear();
			__value__ = x.__value__;
			x.__value__ = nullptr;
			return *this;
		}

		explicit operator bool() const { return __value__ != nullptr; }

		bool operator==(const Ptr<T>& other) const { return __value__ == other.__value__; }

		none clear() {
			if (not __value__) [[unlikely]] return;
			GarbageCollector::remove_root(__value__);
			__value__ = nullptr;
		}

		inline Obj<T>* object() const noexcept { return __value__; }

		inline T& value() const {
			if (__value__) [[likely]] return *(T*)__value__->__data__;
			throw std::runtime_error("Cannot access nullptr of ptr");
		}
		inline T& value() {
			if (__value__) [[likely]] return *(T*)__value__->__data__;
			throw std::runtime_error("Cannot access nullptr of ptr");
		}

		inline std::string address() const {
			return format{} << __value__;
		}

		inline T* c_ptr() const noexcept {
			return __value__ ? static_cast<T*>(__value__->__data__) : nullptr;
		}

		friend format&& operator<<(format&& fm, const Ptr<T>& d) {
			std::move(fm) << d.value();
			return fm;
		}

		template <typename> friend class Ptr;
	};
}