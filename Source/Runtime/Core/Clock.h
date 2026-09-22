#pragma once

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"

#include <cstdint>
#include <string>

namespace NS::Core
{

    //! @brief 高分解能カウンタで経過時間を測る
    //! @details OS のシステム時刻を変えても影響を受けない
    class Clock
    {
    public:
        Clock() = delete;

        //! 戻り値そのものに秒の意味は無い。2 つの値を SecondsBetween へ渡して経過秒にする
        [[nodiscard]] static std::int64_t Now() noexcept;

        //! 長時間起動による精度の低下を防ぐため、戻り値に double を採用している
        [[nodiscard]] static double ElapsedSeconds() noexcept;

        //! Now() で取った 2 つの値の差を秒へ直す。to が from より前なら負を返す
        [[nodiscard]] static double SecondsBetween(std::int64_t from, std::int64_t to) noexcept;
    };

    //! @brief デルタタイムと固定タイムステップを管理する静的クラス
    //! @details 毎フレームの経過時間を蓄積し、固定時間ぶんだけ処理ステップを回す
    class FrameTimer
    {
    public:
        FrameTimer() = delete;

        //! 毎フレームの冒頭で呼び出すこと
        static void Tick() noexcept;

        //! 状態を初期化
        static void Reset() noexcept;

        [[nodiscard]] static float DeltaSeconds() noexcept { return s_delta; }
        [[nodiscard]] static double TotalSeconds() noexcept { return s_total; }
        [[nodiscard]] static std::uint64_t FrameNumber() noexcept { return s_frame; }

        //! デフォルトは毎秒 60 回の固定更新
        static constexpr float k_DefaultFixedDelta = 1.0f / 60.0f;

        //! 固定タイムステップを設定し、0以下はゼロ除算による未定義動作を防ぐため無視
        static void SetFixedDelta(float fixed) noexcept
        {
            if (fixed > 0.0f)
            {
                s_fixedDelta = fixed;
            }
        }
        [[nodiscard]] static float FixedDelta() noexcept { return s_fixedDelta; }
        //! このフレームで固定更新処理を実行すべき回数
        [[nodiscard]] static int FixedStepsThisFrame() noexcept { return s_fixedSteps; }
        //! 描画のガクツキを防ぐための補間用ブレンド率（0.0～1.0）
        [[nodiscard]] static float Alpha() noexcept { return s_accumulator / s_fixedDelta; }

    private:
        static std::int64_t s_lastTime;                         //!< 前回 Tick したときのカウンタ値
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
        ScopedTimer(LogCategory category, std::string label);

        ~ScopedTimer();

        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        LogCategory m_category;
        std::string m_label;
        std::int64_t m_startTime;
    };

} // namespace NS::Core

#define NS_CLOCK_PASTE_IMPL(a, b) a##b
#define NS_CLOCK_PASTE(a, b) NS_CLOCK_PASTE_IMPL(a, b)

//! @brief 指定したスコープの処理時間を計測・ログ出力する
//! @details プロファイリングが無効な環境では何も展開されず、オーバーヘッドは発生しない
#if defined(NS_ENABLE_PROFILING)
#define NS_SCOPED_TIMER(cat, label)                                                                                    \
    ::NS::Core::ScopedTimer NS_CLOCK_PASTE(ns_scoped_timer_, __COUNTER__)((::NS::Core::LogCategory::cat), (label))
#else
#define NS_SCOPED_TIMER(cat, label) ((void)0)
#endif
