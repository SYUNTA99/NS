#pragma once

#include "Runtime/Core/LogType.h"
#include "Runtime/Core/Logger.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

namespace NS::Platform
{

    //! @brief アプリケーション起動時からの経過時間を計測する
    //! @details OS のシステム時刻変更の影響を受けず、<windows.h> にも依存しない
    class Clock
    {
    public:
        Clock() = delete;

        [[nodiscard]] static std::chrono::steady_clock::time_point Now() noexcept
        {
            return std::chrono::steady_clock::now();
        }

        //! 長時間起動による精度の低下を防ぐため、戻り値に double を採用している
        [[nodiscard]] static double ElapsedSeconds() noexcept
        {
            return std::chrono::duration<double>(Now() - StartTime()).count();
        }

    private:
        static const std::chrono::steady_clock::time_point& StartTime() noexcept
        {
            static const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            return start;
        }
    };

    //! @brief デルタタイムと固定タイムステップを管理する静的クラス
    //! @details 毎フレームの経過時間を蓄積し、固定時間ぶんだけ処理ステップを回す
    class FrameTimer
    {
    public:
        FrameTimer() = delete;

        //! 毎フレームの冒頭で呼び出すこと
        static void Tick() noexcept
        {
            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(now - s_lastTime).count();
            s_lastTime = now;
            s_delta = dt;
            s_total += static_cast<double>(dt);
            ++s_frame;

            s_accumulator += dt;
            s_fixedSteps = static_cast<int>(s_accumulator / s_fixedDelta);
            s_accumulator -= static_cast<float>(s_fixedSteps) * s_fixedDelta;
        }

        //! 状態を初期化
        static void Reset() noexcept
        {
            s_lastTime = std::chrono::steady_clock::now();
            s_delta = 0.0f;
            s_total = 0.0;
            s_frame = 0;
            s_accumulator = 0.0f;
            s_fixedSteps = 0;
        }

        //! 前フレームからの経過秒
        [[nodiscard]] static float DeltaSeconds() noexcept { return s_delta; }
        //! 起動からの累計秒
        [[nodiscard]] static double TotalSeconds() noexcept { return s_total; }
        //! 累計フレーム数
        [[nodiscard]] static std::uint64_t FrameNumber() noexcept { return s_frame; }

        //! デフォルトは毎秒 60 回の固定更新
        static constexpr float k_DefaultFixedDelta = 1.0f / 60.0f;

        //! 固定タイムステップを設定し、0以下は無視。0 だと除算結果を int へ変換する箇所が未定義動作になる
        static void SetFixedDelta(float fixed) noexcept
        {
            if (fixed > 0.0f)
            {
                s_fixedDelta = fixed;
            }
        }
        //! 固定更新の間隔（秒）
        [[nodiscard]] static float FixedDelta() noexcept { return s_fixedDelta; }
        //! このフレームで固定更新処理を実行すべき回数
        [[nodiscard]] static int FixedStepsThisFrame() noexcept { return s_fixedSteps; }
        //! 描画のガクツキを防ぐための補間用ブレンド率（0.0～1.0）
        [[nodiscard]] static float Alpha() noexcept { return s_accumulator / s_fixedDelta; }

    private:
        static inline std::chrono::steady_clock::time_point s_lastTime{std::chrono::steady_clock::now()};
        static inline float s_delta = 0.0f;                     //!< 前フレームからの経過秒
        static inline double s_total = 0.0;                     //!< 起動からの累計秒
        static inline std::uint64_t s_frame = 0;                //!< 累計フレーム数
        static inline float s_fixedDelta = k_DefaultFixedDelta; //!< 固定更新の間隔（秒）
        static inline float s_accumulator = 0.0f;               //!< 未消化の経過時間。固定ステップを刻んだ残り
        static inline int s_fixedSteps = 0;                     //!< 今フレームで固定更新を回す回数
    };

    //! @brief スコープを抜ける際に、そこまでの処理にかかった時間をデバッグログに出力する
    //! @details 直接インスタンス化せず、NS_SCOPED_TIMER マクロを経由して使用すること
    class ScopedTimer
    {
    public:
        ScopedTimer(::NS::Core::LogType logType, std::string label)
            : m_logType(logType), m_label(std::move(label)), m_startTime(std::chrono::steady_clock::now())
        {}

        ~ScopedTimer()
        {
            const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(end - m_startTime).count();
            ::NS::Core::Logger::LogImpl(::NS::Core::LogLevel::Debug,
                                        m_logType,
                                        ::magic_enum::enum_name(m_logType),
                                        __FILE__,
                                        __LINE__,
                                        __func__,
                                        ::std::format("{}: {:.3f}ms", m_label, ms));
        }

        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        ::NS::Core::LogType m_logType;
        std::string m_label;
        std::chrono::steady_clock::time_point m_startTime;
    };

} // namespace NS::Platform

#define NS_CLOCK_PASTE_IMPL(a, b) a##b
#define NS_CLOCK_PASTE(a, b) NS_CLOCK_PASTE_IMPL(a, b)

//! @brief 指定したスコープの処理時間を計測・ログ出力する
//! @details プロファイリングが無効な環境では何も展開されず、オーバーヘッドは発生しない
#if defined(NS_ENABLE_PROFILING)
#define NS_SCOPED_TIMER(cat, label)                                                                                    \
    ::NS::Platform::ScopedTimer NS_CLOCK_PASTE(ns_scoped_timer_, __COUNTER__)((::NS::Core::LogType::cat), (label))
#else
#define NS_SCOPED_TIMER(cat, label) ((void)0)
#endif
