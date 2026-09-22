#include "Runtime/Core/Logger.h"

#include "Runtime/Core/StringUtils.h"

#include <windows.h>

#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cassert>
#include <cstdlib>

namespace NS::Core
{

    namespace
    {

        constexpr const char* k_LoggerName = "ns";
        constexpr std::size_t k_RotatingMaxBytes = 5 * 1024 * 1024;
        // ログファイルのバックアップ数。2 なら最新と過去 2 回の 3 ファイルが残る
        constexpr std::size_t k_RotatingMaxFiles = 2;

        std::atomic<bool> g_initialized{false};

        std::string LogsDirectory(const LoggerDesc& desc)
        {
            std::string dir = desc.logDirectory.empty() ? "logs" : desc.logDirectory + "/logs";
            return dir;
        }

        void CreateDirectoryRecursive(std::string_view path)
        {
            const std::wstring wide = StringUtils::WideFromUtf8(path);
            for (std::size_t pos = wide.find_first_of(L"\\/", 1); pos != std::wstring::npos;
                 pos = wide.find_first_of(L"\\/", pos + 1))
            {
                ::CreateDirectoryW(wide.substr(0, pos).c_str(), nullptr);
            }
            ::CreateDirectoryW(wide.c_str(), nullptr);
        }

        spdlog::level::level_enum ToSpdLevel(LogLevel level)
        {
            switch (level)
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
            // 未知の LogLevel が追加されたときにここで気づけるようにする
            assert(false && "Unknown LogLevel");
            return spdlog::level::info;
        }

        std::vector<spdlog::sink_ptr> BuildSinks(const LoggerDesc& desc)
        {
            std::vector<spdlog::sink_ptr> sinks;

            auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
            console->set_pattern("%H:%M:%S.%e [%^%l%$] [%n] %v");
            sinks.push_back(console);

            const auto logsDir = LogsDirectory(desc);
            const std::string logFilePath = logsDir + "/" + desc.logName + ".log";
            auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logFilePath, k_RotatingMaxBytes, k_RotatingMaxFiles, desc.rotateOnOpen);
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

    void Logger::Init(const LoggerDesc& desc) noexcept
    {
        if (g_initialized.exchange(true))
        {
            return;
        }

#if defined(_WIN32)
        // Windowsのコンソールで日本語ログが文字化けしないようにUTF-8に強制する
        ::SetConsoleOutputCP(CP_UTF8);
#endif

        CreateDirectoryRecursive(LogsDirectory(desc));

        auto sinks = BuildSinks(desc);
        auto logger = std::make_shared<spdlog::logger>(k_LoggerName, sinks.begin(), sinks.end());

        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::warn);

        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);
        spdlog::flush_every(std::chrono::seconds(3));

        logger->info("===== 開始 =====");
    }

    void Logger::Shutdown() noexcept
    {
        if (!g_initialized.exchange(false))
        {
            return;
        }
        spdlog::shutdown();
    }

    void Logger::LogImpl(
        LogLevel level, std::string_view category, const char* file, int line, const char* func, std::string_view msg)
    {
        auto logger = spdlog::default_logger();
        if (!logger)
        {
            return;
        }

        spdlog::source_loc loc{file, line, func};
        logger->log(loc, ToSpdLevel(level), "[{}] {}", category, msg);
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

} // namespace NS::Core
