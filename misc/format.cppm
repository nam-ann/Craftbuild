module;

#include <includes.hpp>
#include <ctime>
#include <string>

export module misc.format;

import misc.str;
import misc.list;
import misc.range;
import misc.number;

export namespace craftbuild {
	inline Str time2str(const std::tm& tm, const bool with_date = false) {
		char buffer[20];
		if (with_date) std::strftime(buffer, sizeof(buffer), "%Y/%m/%d %H:%M:%S", &tm);
		else           std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm);
		return Str(buffer);
	}

	inline Str time2file_name(const std::tm& tm) {
		char buffer[20];
		std::strftime(buffer, sizeof(buffer), "%Y.%m.%d %H-%M-%S", &tm);
		return Str(buffer);
	}

	class format {
		Str __buffer__;
	public:
		operator Str() const noexcept {
			return __buffer__;
		}

		friend format&& operator<<(format&& f, const Str& s) {
			f.__buffer__ += s;
			return std::move(f);
		}
		friend format&& operator<<(format&& f, const std::string& s) {
			f.__buffer__ += s;
			return std::move(f);
		}
		friend format&& operator<<(format&& f, std::string_view s) {
			f.__buffer__ += (std::string)s;
			return std::move(f);
		}
		friend format&& operator<<(format&& f, const byte* s) {
			f.__buffer__ += s;
			return std::move(f);
		}
		friend format&& operator<<(format&& f, int64 i) {
			f.__buffer__ += Str(i);
			return std::move(f);
		}
		friend format&& operator<<(format&& f, int32 i) {
			f.__buffer__ += Str(i);
			return std::move(f);
		}
		friend format&& operator<<(format&& f, uint64 i) {
			f.__buffer__ += Str(i);
			return std::move(f);
		}
		friend format&& operator<<(format&& f, uint32 i) {
			f.__buffer__ += Str(i);
			return std::move(f);
		}
		friend format&& operator<<(format&& f, float64 i) {
			f.__buffer__ += Str(i);
			return std::move(f);
		}
		friend format&& operator<<(format&& f, bool b) {
			f.__buffer__ += b ? "true" : "false";
			return std::move(f);
		}
		friend format&& operator<<(format&& f, const void* v) {
			f.__buffer__ += Str(v);
			return std::move(f);
		}
		friend format&& operator<<(format&& f, const std::tm& tm) {
			f.__buffer__ += time2str(tm);
			return std::move(f);
		}
		template <typename T>
		friend format&& operator<<(format&& f, const std::vector<T>& vt) {
			Str result = "[";
			for (auto i : range<usize>(vt.size())) {
				result += Str(vt[i]);
				if (i != vt.size() - 1) result += ", ";
			}
			f.__buffer__ += result + "]";
			return std::move(f);
		}
		template <typename T>
		friend format&& operator<<(format&& f, const List<T>& l) {
			f.__buffer__ += l.str();
			return std::move(f);
		}
	};
}