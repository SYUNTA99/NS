#pragma once

// タイムラインのパネルの、画面に依らない決まり。事象の足し引き・帯の範囲・下見で始まったフレーム・再生の進み
// 出荷ビルドには載らない

#include "Game/Level/HitTimeline.h"

#include <cstddef>
#include <vector>

namespace NS::Editor
{
    struct HitPreviewResult;

    //! @brief 事象を並びの最後へ足す
    //! @details 触れる前に置けない種類 (CanStartBeforeContact が偽) をマイナスへ置こうとした時は 0 から始める。
    //! 読み込みが弾く形をパネルで作らないため。長さは事象の既定 (1)
    //! @param[in,out] timeline 足す先
    //! @param[in] value 足す事象の種類と値
    //! @param[in] start 始まりのフレーム。パネルの再生の位置
    //! @return 足した事象の行の番号
    std::size_t AddHitEvent(NS::Game::Level::HitTimeline& timeline,
                            const NS::Game::Level::HitEventValue& value,
                            int start);

    //! @brief row の行の事象を消し、後ろの行を詰める
    //! @return 消した場合 true。row が範囲の外の場合は false
    bool RemoveHitEvent(NS::Game::Level::HitTimeline& timeline, std::size_t row) noexcept;

    //! @brief 帯に描くフレームの範囲。検知のフレームを 0 にした時計の値で、両端を含む
    struct HitPreviewFrameRange
    {
        int first = 0; //!< 最初のフレーム
        int last = 0;  //!< 最後のフレーム
    };

    //! @brief 事象の始まりと終わり、下見の記録の最初と最後のフレームを全部含む範囲を返す
    //! @param[in] timeline 帯に描くタイムライン
    //! @param[in] preview 下見の結果。nullptr か当たらなかった結果なら事象だけで決める
    //! @return 範囲。事象も下見も無ければ 0 から 0
    [[nodiscard]] HitPreviewFrameRange HitTimelineFrameRange(const NS::Game::Level::HitTimeline& timeline,
                                                             const HitPreviewResult* preview) noexcept;

    //! @brief 下見で row の行の事象が実際に始まったフレームを、検知のフレームを 0 にして返す
    //! @return 始まった順に並ぶ。始まらなかった行は空
    [[nodiscard]] std::vector<int> RowStartFrames(const HitPreviewResult& preview, std::size_t row);

    //! @brief 下見の再生の位置と進み方
    struct HitPreviewPlayback
    {
        int frame = 0;        //!< 今見ているフレーム。下見の frames の添字
        bool playing = false; //!< 再生中か
        float speed = 1.0f;   //!< 速さの倍率。1 は 1 秒に 60 フレーム
        float carry = 0.0f;   //!< まだフレームに届いていない進み

        //! @brief 再生中なら実時間 seconds ぶん進める。lastFrame に着いたら止める
        void Tick(float seconds, int lastFrame) noexcept;

        //! @brief 再生を止めて steps フレームだけ動かす。0 から lastFrame に収める
        void StepBy(int steps, int lastFrame) noexcept;
    };
} // namespace NS::Editor
