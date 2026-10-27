#pragma once

#include "NSlib/Core/LogType.h"

#include <cstddef>
#include <format>
#include <magic_enum/magic_enum.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace NS
{

    //! Fatal は出力後にプロセスを強制終了する
    enum class LogLevel : int
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5,
    };

    struct LoggerDesc
    {
        std::string logName = "ns"; // ログファイル名 (<logName>.log)
        // true の場合、Init のたびに前のログを logName.1.log へローテートする（他プロセスが使用中の場合は追記）
        bool rotateOnOpen = false;
        std::string logDirectory = {}; // 出力先の親ディレクトリ。空の場合は "logs"
    };

    struct RecentLog
    {
        LogLevel level = LogLevel::Warn;
        std::string text; // "[<LogType>] <本文>" 形式
    };

    //! NS_LOG_* から使うスレッド対応のロガー。書式は std::format 準拠
    class Logger
    {
    public:
        static constexpr std::size_t k_RecentLogCapacity = 256;

        Logger() = delete;

        //! Init 以降に出力された WARN 以上のログを新しい順に返す。未初期化時は空
        [[nodiscard]] static std::vector<RecentLog> RecentWarnings();

        //! 複数回呼んでも無視される
        static void Init(const LoggerDesc& desc = {}) noexcept;

        static void Shutdown() noexcept;

        //! NS_LOG_* マクロ内部用。直接呼ばないこと
        static void LogImpl(LogLevel level,
                            std::string_view logTypeStr,
                            const char* file,
                            int line,
                            const char* func,
                            std::string_view msg);

        //! NS_LOG_FATAL マクロ内部用。ログ出力後、非 Shipping ではデバッガで停止してからプロセスを強制終了する
        [[noreturn]] static void FatalImpl(
            std::string_view logTypeStr, const char* file, int line, const char* func, std::string_view msg);
    };

} // namespace NS

#define NS_LOG_IMPL_(lv, cat, ...)                                                                                     \
    ::NS::Logger::LogImpl(                                                                                             \
        (lv), ::magic_enum::enum_name(::NS::LogType::cat), __FILE__, __LINE__, __func__, ::std::format(__VA_ARGS__))

#define NS_LOG_TRACE(cat, ...) NS_LOG_IMPL_(::NS::LogLevel::Trace, cat, __VA_ARGS__)
#define NS_LOG_DEBUG(cat, ...) NS_LOG_IMPL_(::NS::LogLevel::Debug, cat, __VA_ARGS__)
#define NS_LOG_INFO(cat, ...) NS_LOG_IMPL_(::NS::LogLevel::Info, cat, __VA_ARGS__)
#define NS_LOG_WARN(cat, ...) NS_LOG_IMPL_(::NS::LogLevel::Warn, cat, __VA_ARGS__)
#define NS_LOG_ERROR(cat, ...) NS_LOG_IMPL_(::NS::LogLevel::Error, cat, __VA_ARGS__)

#define NS_LOG_FATAL(cat, ...)                                                                                         \
    ::NS::Logger::FatalImpl(                                                                                           \
        ::magic_enum::enum_name(::NS::LogType::cat), __FILE__, __LINE__, __func__, ::std::format(__VA_ARGS__))
