#pragma once

#include "Runtime/Core/LogCategories.h"

#include <format>
#include <magic_enum/magic_enum.hpp>
#include <string_view>

namespace NS::Core
{

    /// ログ出力の重要度。Fatal は出力後にプロセスを強制終了する
    enum class LogLevel : int
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5,
    };

    /// @brief 内部ロガー（spdlog）の実装を隠蔽するクラス。直接は呼ばず NS_LOG_* マクロを使うこと
    /// @details
    /// 各マクロは std::format と同じ書き方ができる。
    /// シングルスレッドでの動作を前提としているため、マルチスレッド環境で同時に叩かないように注意。
    class Logger
    {
    public:
        Logger() = delete;

        /// 失敗時は標準エラー出力にエラーを出し、既定の設定で強行する。複数回呼んでも無視される
        static void Init() noexcept;

        /// ログファイル名を `logs/<name>.log` に変更する。必ずロガーの初期化より前に呼ぶこと（未指定時は "ns"）
        static void SetLogName(std::string_view name) noexcept;

        /// true の場合は起動ごとにファイルを新しくし、false
        /// なら既存のファイルへ追記する。必ずロガーの初期化より前に呼ぶこと
        static void SetRotateOnOpen(bool rotate) noexcept;

        /// 未出力のログをすべてファイル等に書き出して終了処理を行う
        static void Shutdown() noexcept;

        /// NS_LOG_* マクロ内部用。直接呼ばないこと
        static void LogImpl(LogLevel level,
                            std::string_view category,
                            const char* file,
                            int line,
                            const char* func,
                            std::string_view msg);

        /// Fatal マクロ内部用。ログを書き出した後、開発環境ではデバッガで停止し、その後プロセスを強制終了する
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
