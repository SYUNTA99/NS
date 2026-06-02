#pragma once

/// @file Clock.h
/// @brief NS::Core 時刻関連 3 クラス + RAII 計測マクロを単一ヘッダに集約
///
/// - Clock: 起動時刻基準の時刻ソース (all-static)
/// - FrameTimer: ゲームループ tick 管理 + 固定タイムステップ accumulator
/// - ScopedTimer: RAII スコープ計測 (デストラクタで NS_LOG_DEBUG)
/// - NS_SCOPED_TIMER(cat, label): __COUNTER__ ベースの計測マクロ
///
/// 実装は std::chrono::steady_clock 一本、`<windows.h>` 非依存

#include <Framework/Core/LogCategories.h>
#include <Framework/Core/Logger.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

namespace NS::Core
{

    /// 起動時刻基準の時刻ソース。最初の呼び出しで起動時刻を保存 (magic static)
    class Clock
    {
    public:
        Clock() = delete;

        /// 単調増加な現在時刻 (steady_clock の time_point)
        [[nodiscard]] static std::chrono::steady_clock::time_point Now() noexcept
        {
            return std::chrono::steady_clock::now();
        }

        /// プログラム起動からの経過秒。double で長期精度を維持
        [[nodiscard]] static double ElapsedSeconds() noexcept
        {
            return std::chrono::duration<double>(Now() - StartTime()).count();
        }

    private:
        static const std::chrono::steady_clock::time_point& StartTime() noexcept
        {
            static const auto start = std::chrono::steady_clock::now();
            return start;
        }
    };

    /// ゲームループの tick 管理。固定タイムステップ accumulator を内包
    /// all-static、 単一 Application 前提で global state を保持する
    class FrameTimer
    {
    public:
        FrameTimer() = delete;

        /// フレーム冒頭で呼ぶ。delta / total / frame / accumulator を更新する
        static void Tick() noexcept
        {
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(now - s_lastTime).count();
            s_lastTime = now;
            s_delta = dt;
            s_total += static_cast<double>(dt);
            ++s_frame;

            s_accumulator += dt;
            s_fixedSteps = static_cast<int>(s_accumulator / s_fixedDelta);
            s_accumulator -= static_cast<float>(s_fixedSteps) * s_fixedDelta;
        }

        /// 状態を初期化 (FrameNumber=0、accumulator/total=0)。 テスト fixture では SetUp で必ず呼ぶ
        static void Reset() noexcept
        {
            s_lastTime = std::chrono::steady_clock::now();
            s_delta = 0.0f;
            s_total = 0.0;
            s_frame = 0;
            s_accumulator = 0.0f;
            s_fixedSteps = 0;
        }

        [[nodiscard]] static float DeltaSeconds() noexcept { return s_delta; }
        [[nodiscard]] static double TotalSeconds() noexcept { return s_total; }
        [[nodiscard]] static std::uint64_t FrameNumber() noexcept { return s_frame; }

        /// 固定タイムステップを設定 (default 1/60 秒)。ゼロ / 負値は無視 (Tick/Alpha のゼロ除算 UB 防止)
        static void SetFixedDelta(float fixed) noexcept
        {
            if (fixed > 0.0f)
            {
                s_fixedDelta = fixed;
            }
        }
        [[nodiscard]] static float FixedDelta() noexcept { return s_fixedDelta; }
        /// 今回の Tick で OnFixedUpdate を何回呼ぶべきか
        [[nodiscard]] static int FixedStepsThisFrame() noexcept { return s_fixedSteps; }
        /// 描画補間係数 [0, 1)。fixed update 間の中間状態に使う
        [[nodiscard]] static float Alpha() noexcept { return s_accumulator / s_fixedDelta; }

    private:
        static inline std::chrono::steady_clock::time_point s_lastTime{std::chrono::steady_clock::now()};
        static inline float s_delta = 0.0f;
        static inline double s_total = 0.0;
        static inline std::uint64_t s_frame = 0;
        static inline float s_fixedDelta = 1.0f / 60.0f;
        static inline float s_accumulator = 0.0f;
        static inline int s_fixedSteps = 0;
    };

    /// RAII スコープ計測。デストラクタで NS_LOG_DEBUG により経過 ms を出力する
    class ScopedTimer
    {
    public:
        /// `label` は内部で std::string コピー保持するため、一時 std::string の c_str() を渡しても安全
        ScopedTimer(LogCat category, std::string label)
            : m_category(category), m_label(std::move(label)), m_start(std::chrono::steady_clock::now())
        {}

        ~ScopedTimer()
        {
            const auto end = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(end - m_start).count();
            NS_LOG_DEBUG(m_category, "{}: {:.3f}ms", m_label, ms);
        }

        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        LogCat m_category;
        std::string m_label;
        std::chrono::steady_clock::time_point m_start;
    };

} // namespace NS::Core

#define NS_CLOCK_PASTE_IMPL(a, b) a##b
#define NS_CLOCK_PASTE(a, b) NS_CLOCK_PASTE_IMPL(a, b)

/// 計測対象のスコープに置く。デストラクタで `[category] label: X.XXXms` を Debug ログ出力
/// NS_ENABLE_PROFILING define 時のみ有効、 通常 build では no-op (ログの乱発を回避)
/// Profile build は `tools\@build_profile.cmd` で作成する
#if defined(NS_ENABLE_PROFILING)
#define NS_SCOPED_TIMER(cat, label)                                                                                    \
    ::NS::Core::ScopedTimer NS_CLOCK_PASTE(ns_scoped_timer_, __COUNTER__)((cat), (label))
#else
#define NS_SCOPED_TIMER(cat, label) ((void)0)
#endif
