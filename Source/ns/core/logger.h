#pragma once

#include <ns/core/log_categories.h>

#include <magic_enum/magic_enum.hpp>

#include <format>
#include <string_view>

namespace ns::core
{

    /// ログレベル。spdlog の trace/debug/info/warn/error/critical にマッピング。
    /// Fatal は critical 相当 + プロセス停止。
    enum class LogLevel : int
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5,
    };

    /// 静的クラス。Init() を Application::Run() 冒頭、Shutdown() を末尾で呼ぶ。
    /// 公開ヘッダから spdlog の型は一切露出しない（実装側で完全隠蔽）。
    class Logger
    {
    public:
        Logger() = delete;

        /// 全シンク（コンソール / ファイル / msvc debug）を構築する。多重呼び出しは無視。
        static void Init();

        /// 全シンクを flush して破棄する。
        static void Shutdown();

        /// マクロ内部用。直接呼ばないこと。
        static void LogImpl(
            LogLevel lv, std::string_view category, const char* file, int line, const char* func, std::string_view msg);

        /// Fatal: flush → __debugbreak()（Shipping ではスキップ）→ std::abort()。
        [[noreturn]] static void FatalImpl(
            std::string_view category, const char* file, int line, const char* func, std::string_view msg);
    };

} // namespace ns::core

#define NS_LOG_IMPL_(lv, cat, ...)                                                                                     \
    ::ns::core::Logger::LogImpl(                                                                                       \
        (lv), ::magic_enum::enum_name(cat), __FILE__, __LINE__, __func__, ::std::format(__VA_ARGS__))

#define NS_LOG_TRACE(cat, ...) NS_LOG_IMPL_(::ns::core::LogLevel::Trace, cat, __VA_ARGS__)
#define NS_LOG_DEBUG(cat, ...) NS_LOG_IMPL_(::ns::core::LogLevel::Debug, cat, __VA_ARGS__)
#define NS_LOG_INFO(cat, ...) NS_LOG_IMPL_(::ns::core::LogLevel::Info, cat, __VA_ARGS__)
#define NS_LOG_WARN(cat, ...) NS_LOG_IMPL_(::ns::core::LogLevel::Warn, cat, __VA_ARGS__)
#define NS_LOG_ERROR(cat, ...) NS_LOG_IMPL_(::ns::core::LogLevel::Error, cat, __VA_ARGS__)

#define NS_LOG_FATAL(cat, ...)                                                                                         \
    ::ns::core::Logger::FatalImpl(                                                                                     \
        ::magic_enum::enum_name(cat), __FILE__, __LINE__, __func__, ::std::format(__VA_ARGS__))
