#pragma once

/// @file Logger.h
/// @brief NS::Core::Logger — spdlog を隠蔽する静的ファサード + `NS_LOG_*` マクロ群
///
/// @details Application 開始時に `Logger::Init()`、終了時に `Logger::Shutdown()` を呼ぶ
/// 公開ヘッダから spdlog の型は一切露出しない。ログ出力は
/// `NS_LOG_TRACE/DEBUG/INFO/WARN/ERROR/FATAL` マクロで行い、`std::format` 構文の
/// 可変引数を取る。Fatal は flush 後に `__debugbreak()` (Debug 時) → `std::abort()`
/// シングルスレッド前提

#include "Framework/Core/LogCategories.h"

#include <magic_enum/magic_enum.hpp>

#include <format>
#include <string_view>

namespace NS::Core
{

    /// ログレベル。Fatal は critical 相当 + プロセス停止
    enum class LogLevel : int
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5,
    };

    /// 静的クラス。Init() を起動時、Shutdown() を終了時に呼ぶ
    class Logger
    {
    public:
        Logger() = delete;

        /// 全シンクを構築する。失敗時は stderr fallback。多重呼び出しは無視。シングルスレッド前提
        static void Init() noexcept;

        /// ログファイル名を `logs/<name>.log` に設定する。必ず `Init()` の前に呼ぶこと。未指定なら "ns"
        static void SetLogName(std::string_view name) noexcept;

        /// `true` で起動ごとにローテート、`false` で追記。必ず `Init()` の前に呼ぶこと
        static void SetRotateOnOpen(bool rotate) noexcept;

        /// 全シンクを flush して破棄する。シングルスレッド前提
        static void Shutdown() noexcept;

        /// NS_LOG_* マクロ内部用。直接呼ばないこと
        static void LogImpl(LogLevel level,
                            std::string_view category,
                            const char* file,
                            int line,
                            const char* func,
                            std::string_view msg);

        /// Fatal 専用: flush → __debugbreak() (Shipping はスキップ) → std::abort()
        [[noreturn]] static void FatalImpl(
            std::string_view category, const char* file, int line, const char* func, std::string_view msg);
    };

} // namespace NS::Core

#define NS_LOG_IMPL_(lv, cat, ...)                                                                                     \
    ::NS::Core::Logger::LogImpl(                                                                                       \
        (lv), ::magic_enum::enum_name(cat), __FILE__, __LINE__, __func__, ::std::format(__VA_ARGS__))

#define NS_LOG_TRACE(cat, ...) NS_LOG_IMPL_(::NS::Core::LogLevel::Trace, cat, __VA_ARGS__)
#define NS_LOG_DEBUG(cat, ...) NS_LOG_IMPL_(::NS::Core::LogLevel::Debug, cat, __VA_ARGS__)
#define NS_LOG_INFO(cat, ...) NS_LOG_IMPL_(::NS::Core::LogLevel::Info, cat, __VA_ARGS__)
#define NS_LOG_WARN(cat, ...) NS_LOG_IMPL_(::NS::Core::LogLevel::Warn, cat, __VA_ARGS__)
#define NS_LOG_ERROR(cat, ...) NS_LOG_IMPL_(::NS::Core::LogLevel::Error, cat, __VA_ARGS__)

#define NS_LOG_FATAL(cat, ...)                                                                                         \
    ::NS::Core::Logger::FatalImpl(                                                                                     \
        ::magic_enum::enum_name(cat), __FILE__, __LINE__, __func__, ::std::format(__VA_ARGS__))
