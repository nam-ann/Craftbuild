export module misc.format;

import std;

import misc.str;
import misc.list;
import misc.range;
import misc.number;

export namespace craftbuild {
	inline Str time2str(std::tm const& tm, const bool with_date = false) {
		char buffer[20];
		if (with_date) std::strftime(buffer, sizeof(buffer), "%Y/%m/%d %H:%M:%S", &tm);
		else           std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm);
		return Str(buffer);
	}

	inline Str time2file_name(std::tm const& tm) {
		char buffer[20];
		std::strftime(buffer, sizeof(buffer), "%Y.%m.%d %H-%M-%S", &tm);
		return Str(buffer);
	}

	class format {
		Str __buffer__;
	public:
		constexpr format(std::string_view s) : __buffer__(s) {}
		operator Str() const noexcept { return __buffer__; }

		friend format&& operator<<(format&& f, Str const& s) {
			f.__buffer__ += s;
			return std::move(f);
		}
		friend format&& operator<<(format&& f, std::string const& s) {
			f.__buffer__ += s;
			return std::move(f);
		}
		friend format&& operator<<(format&& f, std::string_view s) {
			f.__buffer__ += s;
			return std::move(f);
		}
		friend format&& operator<<(format&& f, char const* s) {
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
		friend format&& operator<<(format&& f, void const* v) {
			f.__buffer__ += Str(v);
			return std::move(f);
		}
		friend format&& operator<<(format&& f, std::tm const& tm) {
			f.__buffer__ += time2str(tm);
			return std::move(f);
		}
		template <typename T>
		friend format&& operator<<(format&& f, std::vector<T> const& vt) {
			Str result = "[";
			for (auto i : range<usize>(vt.size())) {
				result += Str(vt[i]);
				if (i != vt.size() - 1) result += ", ";
			}
			f.__buffer__ += result + "]";
			return std::move(f);
		}
		template <typename T>
		friend format&& operator<<(format&& f, List<T> const& l) {
			f.__buffer__ += l.str();
			return std::move(f);
		}
	};

	constexpr format&& operator""f(char const* c, usize) { return format(c); }
}