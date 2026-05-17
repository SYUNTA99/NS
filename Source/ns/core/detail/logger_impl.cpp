#include <ns/core/logger.h>

#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>

namespace ns::core
{

    namespace
    {

        constexpr const char* kLoggerName = "ns";
        constexpr const char* kLogFilePath = "logs/ns.log";
        constexpr std::size_t kRotatingMaxBytes = 5 * 1024 * 1024;
        constexpr std::size_t kRotatingMaxFiles = 3;

        std::atomic<bool> g_initialized{false};

        spdlog::level::level_enum ToSpdLevel(LogLevel lv)
        {
            switch (lv)
            {
            case LogLevel::Trace:
                return spdlog::level::trace;
            case LogLevel::Debug:
                return spdlog::level::debug;
            case LogLevel::Info:
                return spdlog::level::info;
            case LogLevel::Warn:
                return spdlog::level::warn;
            case LogLevel::Error:
                return spdlog::level::err;
            case LogLevel::Fatal:
                return spdlog::level::critical;
            }
            // LogLevel に値が追加された場合の silent fallthrough を防ぐ
            assert(false && "Unknown LogLevel");
            return spdlog::level::info;
        }

        std::vector<spdlog::sink_ptr> BuildSinks()
        {
            std::vector<spdlog::sink_ptr> sinks;

            auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
            console->set_pattern("%H:%M:%S.%e [%^%l%$] [%n] %v");
            sinks.push_back(console);

            auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                kLogFilePath, kRotatingMaxBytes, kRotatingMaxFiles);
            file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [thread:%t] [%s:%#] %v");
            sinks.push_back(file);

#if defined(_WIN32)
            auto msvc = std::make_shared<spdlog::sinks::msvc_sink_mt>();
            msvc->set_pattern("[%H:%M:%S.%e] [%l] [%n] [%s:%#] %v");
            sinks.push_back(msvc);
#endif

            return sinks;
        }

        [[noreturn]] void DebugBreakAndAbort()
        {
#if defined(NS_SHIPPING)
            std::abort();
#elif defined(_MSC_VER)
            __debugbreak();
            std::abort();
#else
            std::abort();
#endif
        }

    } // namespace

    void Logger::Init()
    {
        if (g_initialized.exchange(true))
        {
            return;
        }

        // 新環境でも初回起動でファイル sink が失敗しないように logs/ を作成しておく
        std::error_code ec;
        std::filesystem::create_directories("logs", ec);

        auto sinks = BuildSinks();
        auto logger = std::make_shared<spdlog::logger>(kLoggerName, sinks.begin(), sinks.end());

        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::warn);

        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);
        spdlog::flush_every(std::chrono::seconds(3));

        logger->info("===== セッション開始 =====");
    }

    void Logger::Shutdown()
    {
        if (!g_initialized.exchange(false))
        {
            return;
        }
        spdlog::shutdown();
    }

    void Logger::LogImpl(
        LogLevel lv, std::string_view category, const char* file, int line, const char* func, std::string_view msg)
    {
        auto logger = spdlog::default_logger();
        if (!logger)
        {
            return;
        }

        spdlog::source_loc loc{file, line, func};
        logger->log(loc, ToSpdLevel(lv), "[{}] {}", category, msg);
    }

    [[noreturn]] void Logger::FatalImpl(
        std::string_view category, const char* file, int line, const char* func, std::string_view msg)
    {
        if (auto logger = spdlog::default_logger())
        {
            spdlog::source_loc loc{file, line, func};
            logger->log(loc, spdlog::level::critical, "[{}] FATAL: {}", category, msg);
            logger->flush();
        }
        DebugBreakAndAbort();
    }

} // namespace ns::core
