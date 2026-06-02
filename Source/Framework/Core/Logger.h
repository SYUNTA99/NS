#pragma once

/// @file Logger.h
/// @brief NS::Core::Logger — spdlog を完全隠蔽する static ファサード + `NS_LOG_*` マクロ群
///
/// @details Application 開始時に `Logger::Init()`、 終了時に `Logger::Shutdown()` を呼ぶ
/// 公開ヘッダから spdlog の型は一切露出しない (実装側で完全隠蔽)。 ログ出力は
/// `NS_LOG_TRACE/DEBUG/INFO/WARN/ERROR/FATAL` マクロで行い、 `std::format` 構文の
/// 可変引数を取る。 Fatal は flush 後に `__debugbreak()` (Debug 時) → `std::abort()`
/// シングルスレッド前提 — マルチスレッド対応は今後判断する

#include <Framework/Core/LogCategories.h>

#include <magic_enum/magic_enum.hpp>

#include <format>
#include <string_view>

namespace NS::Core
{

    /// ログレベル。spdlog の trace/debug/info/warn/error/critical にマッピング
    /// Fatal は critical 相当 + プロセス停止
    enum class LogLevel : int
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5,
    };

    /// 静的クラス。Init() を Application::Run() 冒頭、Shutdown() を末尾で呼ぶ
    /// 公開ヘッダから spdlog の型は一切露出しない（実装側で完全隠蔽）
    class Logger
    {
    public:
        Logger() = delete;

        /// 全シンク（コンソール / ファイル / msvc debug）を構築する。多重呼び出しは無視
        /// 構築失敗 (spdlog の file sink コンストラクタ等) は内部で握り潰し、 stderr に fallback log を
        /// 出して `g_initialized` を解除する (呼出側は再試行可能、 例外は伝播しない)
        /// @warning シングルスレッド前提。複数スレッドからの同時呼び出しは未定義動作
        static void Init() noexcept;

        /// ログファイル名を `logs/<name>.log` 形式で指定する。 必ず `Init()` の **前** に呼ぶこと
        /// 後から呼んでも既に開かれた spdlog file sink には影響しない。 Game と Tests でログを
        /// 物理分離するために使う (Game → "game"、 Tests → "tests")。 未指定なら "ns"
        static void SetLogName(std::string_view name) noexcept;

        /// `Init()` で file sink を開く時に既存 `<name>.log` を rotate するかを切替える
        /// `true` (default false) で 「起動ごとに 1 ファイル」 運用 (Game 推奨)、
        /// `false` で 「session 跨ぎ追記」 運用 (Tests 推奨、 test fixture が Init/Shutdown を
        /// 繰り返しても 1 つの `<name>.log` に蓄積される)。 必ず `Init()` の前に呼ぶこと
        static void SetRotateOnOpen(bool rotate) noexcept;

        /// 全シンクを flush して破棄する
        /// @warning シングルスレッド前提。Init() と並行・競合させないこと
        static void Shutdown() noexcept;

        /// マクロ内部用。直接呼ばないこと
        static void LogImpl(
            LogLevel lv, std::string_view category, const char* file, int line, const char* func, std::string_view msg);

        /// Fatal: flush → __debugbreak()（Shipping ではスキップ）→ std::abort()
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
