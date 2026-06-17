#include <Framework/Core/Logger.h>

#include "Framework/Framework.h"

#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace NS::Core
{

    namespace
    {

        constexpr const char* kLoggerName = "ns";
        constexpr std::size_t kRotatingMaxBytes = 5 * 1024 * 1024;
        // spdlog の max_files は rotated backup の本数で、 current 含め計 (N+1) 個
        // 2 指定で `<name>.log` + `.1.log` + `.2.log` の 3 ファイル運用
        constexpr std::size_t kRotatingMaxFiles = 2;

        std::atomic<bool> g_initialized{false};
        // SetLogName で上書き可能なログ stem。 Init() 前に書込まれる前提で std::string、
        // 既定値 "ns" で従来挙動を維持
        std::string g_logName{"ns"};
        // 起動ごとに rotate する。 Tests は SetRotateOnOpen(false) して
        // 1 ファイル蓄積モードに切替える。 既定 false で従来挙動を維持
        bool g_rotateOnOpen{false};

        /// 実行 exe の絶対ディレクトリを取得する。 取得失敗時は空 path
        std::filesystem::path GetExeDirectory() noexcept
        {
            wchar_t buffer[MAX_PATH];
            const DWORD len = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (len == 0 || len >= MAX_PATH)
            {
                return std::filesystem::path{};
            }
            return std::filesystem::path{buffer}.parent_path();
        }

        /// ログ出力先の絶対パス。 premake5.lua / .git を上位へ辿りリポジトリルート直下の `logs/` を返す
        /// ルート検出失敗 (shipping 配布) は exe 同階層の `logs/` に fallback
        std::filesystem::path GetLogsDirectory() noexcept
        {
            const auto exeDir = GetExeDirectory();
            if (exeDir.empty())
                return std::filesystem::path{};

            for (auto dir = exeDir; !dir.empty();)
            {
                std::error_code ec;
                if (std::filesystem::exists(dir / "premake5.lua", ec) || std::filesystem::exists(dir / ".git", ec))
                {
                    return dir / "logs";
                }
                auto parent = dir.parent_path();
                if (parent == dir)
                    break;
                dir = parent;
            }
            return exeDir / "logs";
        }

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

            const auto logsDir = GetLogsDirectory();
            const std::string logFilePath =
                logsDir.empty() ? ("logs/" + g_logName + ".log") : (logsDir / (g_logName + ".log")).string();
            // rotate_on_open: Game は起動ごと rotate (per-session log)、 Tests は false で
            // 1 Tests.exe 内の test fixture の Init/Shutdown サイクルを 1 つの tests.log に蓄積
            auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logFilePath, kRotatingMaxBytes, kRotatingMaxFiles, g_rotateOnOpen);
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

    void Logger::SetLogName(std::string_view name) noexcept
    {
        // 空入力は無視 (既定 "ns" のまま)。 Init() 後の呼出は既に開かれた file sink には反映
        // されないが、 後続の Shutdown → Init の組合せで効くため state は更新しておく
        if (name.empty())
            return;
        g_logName.assign(name);
    }

    void Logger::SetRotateOnOpen(bool rotate) noexcept
    {
        g_rotateOnOpen = rotate;
    }

    void Logger::Init() noexcept
    {
        if (g_initialized.exchange(true))
        {
            return;
        }

#if defined(_WIN32)
        // コンソール出力を UTF-8 に固定し、 呼び出し側の chcp に依存せず日本語ログの文字化けを防ぐ
        ::SetConsoleOutputCP(CP_UTF8);
#endif

        try
        {
            // 初回起動でファイル sink が失敗しないよう logs/ を先に作成する
            // 場所の優先順は GetLogsDirectory と同じ (リポジトリルート → exe 同階層)
            std::error_code ec;
            const auto logsDir = GetLogsDirectory();
            std::filesystem::create_directories(logsDir.empty() ? std::filesystem::path{"logs"} : logsDir, ec);

            auto sinks = BuildSinks();
            auto logger = std::make_shared<spdlog::logger>(kLoggerName, sinks.begin(), sinks.end());

            logger->set_level(spdlog::level::trace);
            logger->flush_on(spdlog::level::warn);

            spdlog::register_logger(logger);
            spdlog::set_default_logger(logger);
            spdlog::flush_every(std::chrono::seconds(3));

            logger->info("===== セッション開始 =====");
        }
        catch (const std::exception& e)
        {
            // spdlog 構築失敗時は logger が未構築なので NS_LOG_ERROR 不可、 stderr に直接出す
            std::fprintf(stderr, "Logger::Init failed: %s\n", e.what());
            g_initialized.store(false);
        }
        catch (...)
        {
            std::fprintf(stderr, "Logger::Init failed: unknown error\n");
            g_initialized.store(false);
        }
    }

    void Logger::Shutdown() noexcept
    {
        if (!g_initialized.exchange(false))
        {
            return;
        }
        try
        {
            spdlog::shutdown();
        }
        catch (...)
        {
            // shutdown 中の例外は無視 (ログ出口を閉じている最中なので報告先がない)
        }
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

} // namespace NS::Core
