#pragma once

// 当たりのタイムラインの、画面に依らない決まり
// 事象の足し引き・帯の範囲・下見で始まったフレーム
// 出荷ビルドには載らない

#include "Editor/Timeline.h"
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
    std::size_t AddHitEvent(GL::Level::HitTimeline& timeline,
                            const GL::Level::HitEventValue& value,
                            int start);

    //! @brief row の行の事象を消し、後ろの行を詰める
    //! @return 消した場合 true。row が範囲の外の場合は false
    bool RemoveHitEvent(GL::Level::HitTimeline& timeline, std::size_t row) noexcept;

    //! @brief 事象の始まりと終わり、下見の記録の最初と最後のフレームを全部含む範囲を返す
    //! @param[in] timeline 帯に描くタイムライン
    //! @param[in] preview 下見の結果。nullptr か当たらなかった結果なら事象だけで決める
    //! @return 範囲。事象も下見も無ければ 0 から 0
    [[nodiscard]] TimelineFrameRange HitTimelineFrameRange(const GL::Level::HitTimeline& timeline,
                                                           const HitPreviewResult* preview) noexcept;

    //! @brief 下見で row の行の事象が実際に始まったフレームを、検知のフレームを 0 にして返す
    //! @return 始まった順に並ぶ。始まらなかった行は空
    [[nodiscard]] std::vector<int> RowStartFrames(const HitPreviewResult& preview, std::size_t row);

} // namespace NS::Editor
