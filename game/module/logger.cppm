module;

#pragma warning(push, 0)
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#pragma warning(pop)

#include <includes.hpp>
#include <mutex>
#include <thread>
#include <string>
#include <fstream>
#include <filesystem>
#include <source_location>

#define LOC_PARAM std::source_location __loc__

export module game.logger;

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

    auto get_tm_time = []() {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm;
#ifdef _WIN32
        localtime_s(&now_tm, &now_time_t);
#else
        localtime_r(&now_time_t, &now_tm);
#endif
        return now_tm;
    };

    auto get_time = [] -> Str {
        return format{} << "[" << get_tm_time() << "] ";
    };

    auto get_info = [](LOC_PARAM) -> Str {
        return format{} << "(" << ThreadRegistry::get_name(std::this_thread::get_id()) << " | " << get_file_name(__loc__.file_name()) << ":" << __loc__.line() << ") ";
    };
}

export namespace craftbuild {
    struct LogQueue {
        inline static Str file_queue;
        inline static std::mutex log_mutex;

        static none store(Str const& log, Str const& file_log) {
            if (craftbuild_debug) UtilityFunctions::print(log.std_str().c_str());

            std::lock_guard<std::mutex> lock(log_mutex);
            file_queue += file_log + "\n";
        }

        static none flush() {
            Str file_dump;

            {
                std::lock_guard<std::mutex> lock(log_mutex);
                file_dump.swap(file_queue);
            }

            if (not file_dump) return;

            static auto log_file = []() {
                const auto time = get_tm_time();
                const String real_path = ProjectSettings::get_singleton()->globalize_path(("user://game/logs/" + time2file_name(time) + ".txt").std_str().c_str());
                const std::string std_path = real_path.utf8().get_data();
                std::filesystem::create_directories(std::filesystem::path(std_path).parent_path());
                return std::ofstream(std_path);
            }();

            log_file << file_dump << std::flush;
        }
    };

    enum class LogType : uint8 {
        NORMAL,
        VERBOSE,
        INFO,
        WARNING,
        ERROR
    };

    template <LogType LOG_TYPE>
    none log(Str const& message, std::source_location __loc__ = std::source_location::current()) {}

    template <>
    none log<LogType::NORMAL>(Str const& message, LOC_PARAM) {
        const Str time = get_time();
        const Str info = get_info(__loc__);

        const Str current_log = format{} << time << "None:    " << info << message;
        Str log;

        if (colored_log) log += format{} << "\033[97m" << time << "None:    " << info << "\033[37m" << message << "\033[0m";
        else log = current_log;

        LogQueue::store(log, current_log);
    }

    template <>
    none log<LogType::VERBOSE>(Str const& message, LOC_PARAM) {
        if (not log_verbose) return;

        const Str time = get_time();
        const Str info = get_info(__loc__);

        const Str current_log = format{} << time << "Verbose: " << info << message;
        Str log;

        if (colored_log) log += format{} << "\033[90m" << time << "Verbose: " << info << message << "\033[0m";
        else log = current_log;

        LogQueue::store(log, current_log);
    }

    template <>
    none log<LogType::INFO>(Str const& message, LOC_PARAM) {
        const Str time = get_time();
        const Str info = get_info(__loc__);

        const Str current_log = format{} << time << "Info:    " << info << message;
        Str log;

        if (colored_log) log += format{} << "\033[96m" << time << "Info:    " << info << "\033[36m" << message << "\033[0m";
        else log = current_log;

        LogQueue::store(log, current_log);
    }

    template <>
    none log<LogType::WARNING>(Str const& message, LOC_PARAM) {
        const Str time = get_time();
        const Str info = get_info(__loc__);

        const Str current_log = format{} << time << "Warning: " << info << message;
        Str log;

        if (colored_log) log += format{} << "\033[93m" << time << "Warning: " << info << "\033[33m" << message << "\033[0m";
        else log = current_log;

        LogQueue::store(log, current_log);
    }

    template <>
    none log<LogType::ERROR>(Str const& message, LOC_PARAM) {
        const Str time = get_time();
        const Str info = get_info(__loc__);

        const Str current_log = format{} << time << "Error:   " << info << message;
        Str log;

        if (colored_log) log += format{} << "\033[91m" << time << "Error:   " << info << "\033[31m" << message << "\033[0m";
        else log = current_log;

        LogQueue::store(log, current_log);
    }
}