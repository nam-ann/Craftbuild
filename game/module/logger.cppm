export module game.logger;

import std;

import misc.str;
import misc.number;
import misc.format;
import game.core;
import game.thread;

using namespace godot;

namespace craftbuild {
    constexpr std::string_view get_file_name(std::string_view path) {
        auto last_slash = path.find_last_of("\\/");
        auto start_pos = (last_slash == std::string_view::npos) ? 0 : last_slash + 1;

        std::string_view filename_with_ext = path.substr(start_pos);
        auto last_dot = filename_with_ext.find_last_of('.');

        if (last_dot != std::string_view::npos and last_dot != 0) return filename_with_ext.substr(0, last_dot);
        return filename_with_ext;
    }
}

export namespace craftbuild {
    struct LogQueue {
        inline static Str file_queue;
        inline static std::mutex log_mutex;

        static void store(Str const& log, Str const& file_log);
        static void flush();
    };

    enum class LogType : uint8 {
        NORMAL,
        VERBOSE,
        INFO,
        WARNING,
        ERROR
    };

    template <LogType LOG_TYPE>
    void log(Str const& message, std::source_location __loc__ = std::source_location::current());
}