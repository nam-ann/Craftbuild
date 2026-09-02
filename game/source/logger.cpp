module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/project_settings.hpp>
ENABLE_WARNING

#define LOC_PARAM std::source_location __loc__

module game.logger;

namespace craftbuild {
    static auto get_tm_time() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);

        return *std::localtime(&time);
    }

    static Str get_time()  {
        return "["f << get_tm_time() << "] ";
    }

    static Str get_info(LOC_PARAM) {
        return "("f << ThreadRegistry::get_name(std::this_thread::get_id()) << " | " << get_file_name(__loc__.file_name()) << ":" << __loc__.line() << ") ";
    }

    void LogQueue::store(Str const& log, Str const& file_log) {
        if (craftbuild_debug) UtilityFunctions::print(log.std_str().c_str());

        std::lock_guard<std::mutex> lock(log_mutex);
        file_queue += file_log + "\n";
    }

    void LogQueue::flush() {
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

    template <>
    void log<LogType::NORMAL>(Str const& message, LOC_PARAM) {
        const Str time = get_time();
        const Str info = get_info(__loc__);

        const Str current_log = ""f << time << "None:    " << info << message;
        Str log;

        if (colored_log) log += "\033[97m"f << time << "None:    " << info << "\033[37m" << message << "\033[0m";
        else log = current_log;

        LogQueue::store(log, current_log);
    }

    template <>
    void log<LogType::VERBOSE>(Str const& message, LOC_PARAM) {
        if (not log_verbose) return;

        const Str time = get_time();
        const Str info = get_info(__loc__);

        const Str current_log = ""f << time << "Verbose: " << info << message;
        Str log;

        if (colored_log) log += "\033[90m"f << time << "Verbose: " << info << message << "\033[0m";
        else log = current_log;

        LogQueue::store(log, current_log);
    }

    template <>
    void log<LogType::INFO>(Str const& message, LOC_PARAM) {
        const Str time = get_time();
        const Str info = get_info(__loc__);

        const Str current_log = ""f << time << "Info:    " << info << message;
        Str log;

        if (colored_log) log += "\033[96m"f << time << "Info:    " << info << "\033[36m" << message << "\033[0m";
        else log = current_log;

        LogQueue::store(log, current_log);
    }

    template <>
    void log<LogType::WARNING>(Str const& message, LOC_PARAM) {
        const Str time = get_time();
        const Str info = get_info(__loc__);

        const Str current_log = ""f << time << "Warning: " << info << message;
        Str log;

        if (colored_log) log += "\033[93m"f << time << "Warning: " << info << "\033[33m" << message << "\033[0m";
        else log = current_log;

        LogQueue::store(log, current_log);
    }

    template <>
    void log<LogType::ERROR>(Str const& message, LOC_PARAM) {
        const Str time = get_time();
        const Str info = get_info(__loc__);

        const Str current_log = ""f << time << "Error:   " << info << message;
        Str log;

        if (colored_log) log += "\033[91m"f << time << "Error:   " << info << "\033[31m" << message << "\033[0m";
        else log = current_log;

        LogQueue::store(log, current_log);
    }
}