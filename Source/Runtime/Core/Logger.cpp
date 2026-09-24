#include "Runtime/Core/Logger.h"

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
            return desc.logDirectory.empty() ? "logs" : desc.logDirectory + "/logs";
        }

        // Core は Platform に依存できないので、StringUtils を使わずここで変換する
        std::wstring WidenPath(std::string_view utf8)
        {
            if (utf8.empty())
            {
                return {};
            }
            const int srcLen = static_cast<int>(utf8.size());
            const int dstLen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), srcLen, nullptr, 0);
            if (dstLen <= 0)
            {
                return {};
            }
            std::wstring result(static_cast<std::size_t>(dstLen), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), srcLen, result.data(), dstLen);
            return result;
        }

        void CreateDirectoryRecursive(std::string_view path)
        {
            const std::wstring wide = WidenPath(path);
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

            std::shared_ptr<spdlog::sinks::stderr_color_sink_mt> console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
            console->set_pattern("%H:%M:%S.%e [%^%l%$] [%n] %v");
            sinks.push_back(console);

            const std::string logsDir = LogsDirectory(desc);
            const std::string logFilePath = logsDir + "/" + desc.logName + ".log";
            std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logFilePath, k_RotatingMaxBytes, k_RotatingMaxFiles, desc.rotateOnOpen);
            file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [thread:%t] [%s:%#] %v");
            sinks.push_back(file);

#if defined(_WIN32)
            std::shared_ptr<spdlog::sinks::msvc_sink_mt> msvc = std::make_shared<spdlog::sinks::msvc_sink_mt>();
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

        std::vector<spdlog::sink_ptr> sinks = BuildSinks(desc);
        std::shared_ptr<spdlog::logger> logger = std::make_shared<spdlog::logger>(k_LoggerName, sinks.begin(), sinks.end());

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

    void Logger::LogImpl(LogLevel level,
                         LogType logType,
                         std::string_view logTypeStr,
                         const char* file,
                         int line,
                         const char* func,
                         std::string_view msg)
    {
        std::shared_ptr<spdlog::logger> logger = spdlog::default_logger();
        if (!logger)
        {
            return;
        }

        spdlog::source_loc loc{file, line, func};
        logger->log(loc, ToSpdLevel(level), "[{}] {}", logTypeStr, msg);
    }

    [[noreturn]] void Logger::FatalImpl(LogType logType,
                                        std::string_view logTypeStr,
                                        const char* file,
                                        int line,
                                        const char* func,
                                        std::string_view msg)
    {
        if (std::shared_ptr<spdlog::logger> logger = spdlog::default_logger())
        {
            spdlog::source_loc loc{file, line, func};
            logger->log(loc, spdlog::level::critical, "[{}] FATAL: {}", logTypeStr, msg);
            logger->flush();
        }
        DebugBreakAndAbort();
    }

} // namespace NS::Core
