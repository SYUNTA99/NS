#pragma once

#include "Runtime/Core/LogCategories.h"

#include <format>
#include <magic_enum/magic_enum.hpp>
#include <string>
#include <string_view>

namespace NS::Core
{

    //! ログ出力の重要度。Fatal は出力後にプロセスを強制終了する
    enum class LogLevel : int
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5,
    };

    //! @brief ロガー初期化用の設定
    struct LoggerDesc
    {
        //! ログの識別名。ログファイル名に使う
        std::string logName = "ns";

        //! true の場合、Init のたびに新しいログファイルへ切り替える
        bool rotateOnOpen = false;

        //! ログ出力先の親ディレクトリ。空の場合は logs だけを使う
        std::string logDirectory = {};
    };

    //! @brief spdlog を隠すロガー。直接は呼ばず NS_LOG_* マクロを使うこと
    //! @details
    //! 各マクロは std::format と同じ書き方ができる
    //! sink は spdlog の _mt 系を使うため、複数スレッドから同時に呼んでも安全
    class Logger
    {
    public:
        Logger() = delete;

        //! 失敗時は標準エラー出力にエラーを出し、既定の設定で強行する。複数回呼んでも無視される
        static void Init(const LoggerDesc& desc = {}) noexcept;

        //! 未出力のログをすべて書き出して終了する
        static void Shutdown() noexcept;

        //! NS_LOG_* マクロ内部用。直接呼ばないこと
        static void LogImpl(LogLevel level,
                            std::string_view category,
                            const char* file,
                            int line,
                            const char* func,
                            std::string_view msg);

        //! Fatal マクロ内部用。ログを書き出した後、開発環境ではデバッガで停止し、その後プロセスを強制終了する
        [[noreturn]] static void FatalImpl(
            std::string_view category, const char* file, int line, const char* func, std::string_view msg);
    };

} // namespace NS::Core

#define NS_LOG_IMPL_(lv, cat, ...)                                                                                     \
    ::NS::Core::Logger::LogImpl((lv),                                                                                  \
                                ::magic_enum::enum_name(::NS::Core::LogCategory::cat),                                 \
                                __FILE__,                                                                              \
                                __LINE__,                                                                              \
                                __func__,                                                                              \
                                ::std::format(__VA_ARGS__))

#define NS_LOG_TRACE(cat, ...) NS_LOG_IMPL_(::NS::Core::LogLevel::Trace, cat, __VA_ARGS__)
#define NS_LOG_DEBUG(cat, ...) NS_LOG_IMPL_(::NS::Core::LogLevel::Debug, cat, __VA_ARGS__)
#define NS_LOG_INFO(cat, ...) NS_LOG_IMPL_(::NS::Core::LogLevel::Info, cat, __VA_ARGS__)
#define NS_LOG_WARN(cat, ...) NS_LOG_IMPL_(::NS::Core::LogLevel::Warn, cat, __VA_ARGS__)
#define NS_LOG_ERROR(cat, ...) NS_LOG_IMPL_(::NS::Core::LogLevel::Error, cat, __VA_ARGS__)

#define NS_LOG_FATAL(cat, ...)                                                                                         \
    ::NS::Core::Logger::FatalImpl(::magic_enum::enum_name(::NS::Core::LogCategory::cat),                               \
                                  __FILE__,                                                                            \
                                  __LINE__,                                                                            \
                                  __func__,                                                                            \
                                  ::std::format(__VA_ARGS__))
