#include "NSlib/Core/Logger.h"

#include <windows.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace NS
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
            if (desc.logDirectory.empty())
            {
                return "logs";
            }
            return desc.logDirectory + "/logs";
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

        std::string LogFilePath(const std::string& logsDir, const std::string& logName, std::size_t backupIndex)
        {
            if (backupIndex == 0)
            {
                return logsDir + "/" + logName + ".log";
            }
            return logsDir + "/" + logName + "." + std::to_string(backupIndex) + ".log";
        }

        // 前のログを logName.1.log へ、それまでの控えを 1 つ後ろへずらす。
        // 他のプログラムが開いていて名前を変えられない時は、そのまま今のファイルへ追記する。
        // spdlog の回転は名前の変更の失敗で abort するので使わない
        void RotateLogFiles(const std::string& logsDir, const std::string& logName)
        {
            for (std::size_t index = k_RotatingMaxFiles; index > 0; --index)
            {
                const std::wstring from = WidenPath(LogFilePath(logsDir, logName, index - 1));
                const std::wstring to = WidenPath(LogFilePath(logsDir, logName, index));
                ::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING);
            }
        }

        bool IsLargerThan(std::string_view path, std::size_t bytes)
        {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (!::GetFileAttributesExW(WidenPath(path).c_str(), GetFileExInfoStandard, &data))
            {
                return false;
            }
            ULARGE_INTEGER size{};
            size.LowPart = data.nFileSizeLow;
            size.HighPart = data.nFileSizeHigh;
            return size.QuadPart > bytes;
        }

        bool CanAppendTo(std::string_view path)
        {
            const HANDLE handle = ::CreateFileW(WidenPath(path).c_str(),
                                                FILE_APPEND_DATA,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                nullptr,
                                                OPEN_ALWAYS,
                                                FILE_ATTRIBUTE_NORMAL,
                                                nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            ::CloseHandle(handle);
            return true;
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

            std::shared_ptr<spdlog::sinks::stderr_color_sink_mt> console =
                std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
            console->set_pattern("%H:%M:%S.%e [%^%l%$] [%n] %v");
            sinks.push_back(console);

            const std::string logsDir = LogsDirectory(desc);
            const std::string logFilePath = LogFilePath(logsDir, desc.logName, 0);
            // 実行中は大きさで回さないので、大きくなったログは次の起動で控えへ回す
            if (desc.rotateOnOpen || IsLargerThan(logFilePath, k_RotatingMaxBytes))
            {
                RotateLogFiles(logsDir, desc.logName);
            }
            // 例外を切った spdlog は開けないファイルで abort するので、書けない場所ではファイルへ出さない
            if (CanAppendTo(logFilePath))
            {
                std::shared_ptr<spdlog::sinks::basic_file_sink_mt> file =
                    std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath);
                file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [thread:%t] [%s:%#] %v");
                sinks.push_back(file);
            }
            else
            {
                std::fprintf(
                    stderr, "Logger: ログファイルを開けないのでファイルへは出さない (%s)\n", logFilePath.c_str());
            }

            std::shared_ptr<spdlog::sinks::msvc_sink_mt> msvc = std::make_shared<spdlog::sinks::msvc_sink_mt>();
            msvc->set_pattern("[%H:%M:%S.%e] [%l] [%n] [%s:%#] %v");
            sinks.push_back(msvc);
            return sinks;
        }

        [[noreturn]] void DebugBreakAndAbort()
        {
#if !defined(NS_SHIPPING)
            __debugbreak();
#endif
            std::abort();
        }

    } // namespace

    void Logger::Init(const LoggerDesc& desc) noexcept
    {
        if (g_initialized.exchange(true))
        {
            return;
        }

        // Windowsのコンソールで日本語ログが文字化けしないようにUTF-8に強制する
        ::SetConsoleOutputCP(CP_UTF8);

        CreateDirectoryRecursive(LogsDirectory(desc));

        std::vector<spdlog::sink_ptr> sinks = BuildSinks(desc);
        std::shared_ptr<spdlog::logger> logger =
            std::make_shared<spdlog::logger>(k_LoggerName, sinks.begin(), sinks.end());

        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::warn);

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
        LogLevel level, std::string_view logTypeStr, const char* file, int line, const char* func, std::string_view msg)
    {
        std::shared_ptr<spdlog::logger> logger = spdlog::default_logger();
        if (!logger)
        {
            return;
        }

        spdlog::source_loc loc{file, line, func};
        logger->log(loc, ToSpdLevel(level), "[{}] {}", logTypeStr, msg);
    }

    [[noreturn]] void Logger::FatalImpl(
        std::string_view logTypeStr, const char* file, int line, const char* func, std::string_view msg)
    {
        if (std::shared_ptr<spdlog::logger> logger = spdlog::default_logger())
        {
            spdlog::source_loc loc{file, line, func};
            logger->log(loc, spdlog::level::critical, "[{}] FATAL: {}", logTypeStr, msg);
            logger->flush();
        }
        DebugBreakAndAbort();
    }

} // namespace NS
