#include "NSlib/Core/Logger.h"

#include "NSlib/Core/Windows/Filesystem.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace NS
{

    namespace
    {

        constexpr const char* k_LoggerName = "ns";
        constexpr uint64 k_RotatingMaxBytes = 5 * 1024 * 1024;
        // ログファイルのバックアップ数。2 なら最新と過去 2 回の 3 ファイルが残る
        constexpr std::size_t k_RotatingMaxFiles = 2;

        std::atomic<bool> g_initialized{false};

        // WARN 以上を溜める出し先。Init で作り、Shutdown で捨てる。読むのはエディタのコンソール
        std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> g_recentSink;

        std::string LogsDirectory(const LoggerDesc& desc)
        {
            if (desc.logDirectory.empty())
            {
                return "logs";
            }
            return desc.logDirectory + "/logs";
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
                const std::string from = LogFilePath(logsDir, logName, index - 1);
                if (OS::FileSystem::Exists(from))
                {
                    (void)OS::FileSystem::RenameFile(from, LogFilePath(logsDir, logName, index));
                }
            }
        }

        // 実行中は大きさで回さないので、大きくなったログは次の起動で控えへ回す
        bool IsLargerThan(const std::string& path, uint64 bytes)
        {
            if (!OS::FileSystem::Exists(path))
            {
                return false;
            }
            const std::optional<uint64> size = OS::FileSystem::FileSize(path);
            return size.has_value() && *size > bytes;
        }

        // spdlog の basic_file_sink は開けないファイルで abort し、ほかのプログラムとの共有も選べないので、
        // 開くのと書くのを FileAppender に任せる
        class FileAppenderSink : public spdlog::sinks::base_sink<std::mutex>
        {
        public:
            explicit FileAppenderSink(std::unique_ptr<OS::FileAppender> file) noexcept : m_file(std::move(file)) {}

        protected:
            void sink_it_(const spdlog::details::log_msg& msg) override
            {
                spdlog::memory_buf_t formatted;
                formatter_->format(msg, formatted);
                // 書けなかった事をログに出すと、この出力先のロックを持ったまま同じ出力先へ戻ってきて止まる
                (void)m_file->Append(std::as_bytes(std::span<const char>(formatted.data(), formatted.size())));
            }

            // FileAppender は書くたびに OS へ渡すので、プログラムが止まっても書いた分は残る。
            // ディスクへの書き込みを待つと、警告のたびにフレームが止まる
            void flush_() override {}

        private:
            std::unique_ptr<OS::FileAppender> m_file;
        };

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

        LogLevel FromSpdLevel(spdlog::level::level_enum level)
        {
            switch (level)
            {
            case spdlog::level::trace:
                return LogLevel::Trace;
            case spdlog::level::debug:
                return LogLevel::Debug;
            case spdlog::level::info:
                return LogLevel::Info;
            case spdlog::level::warn:
                return LogLevel::Warn;
            case spdlog::level::err:
                return LogLevel::Error;
            case spdlog::level::critical:
                return LogLevel::Fatal;
            case spdlog::level::off:
            case spdlog::level::n_levels:
                break;
            }
            // 出し先は off を受けないので、ここへは来ない
            assert(false && "Unknown spdlog level");
            return LogLevel::Info;
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
            if (desc.rotateOnOpen || IsLargerThan(logFilePath, k_RotatingMaxBytes))
            {
                RotateLogFiles(logsDir, desc.logName);
            }
            // 開けない時のエラーは FileAppender が出す。ファイルへは出さずに続ける
            std::unique_ptr<OS::FileAppender> file = std::make_unique<OS::FileAppender>(logFilePath);
            if (file->IsValid())
            {
                std::shared_ptr<FileAppenderSink> fileSink = std::make_shared<FileAppenderSink>(std::move(file));
                fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [thread:%t] [%s:%#] %v");
                sinks.push_back(fileSink);
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

        (void)OS::FileSystem::CreateDirectories(LogsDirectory(desc));

        std::vector<spdlog::sink_ptr> sinks = BuildSinks(desc);
        // 作るのも捨てるのも Init と Shutdown で、ロガーに渡した出し先だけを読む
        g_recentSink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(Logger::k_RecentLogCapacity);
        g_recentSink->set_level(spdlog::level::warn);
        sinks.push_back(g_recentSink);
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
        g_recentSink.reset();
    }

    std::vector<RecentLog> Logger::RecentWarnings()
    {
        std::vector<RecentLog> recent;
        if (!g_recentSink)
        {
            return recent;
        }
        // last_raw は古い順に返す
        const std::vector<spdlog::details::log_msg_buffer> stored = g_recentSink->last_raw();
        recent.reserve(stored.size());
        for (std::vector<spdlog::details::log_msg_buffer>::const_reverse_iterator it = stored.rbegin();
             it != stored.rend();
             ++it)
        {
            recent.push_back(RecentLog{FromSpdLevel(it->level), std::string(it->payload.data(), it->payload.size())});
        }
        return recent;
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
