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

    //! 末尾に事象を足し、行番号を返す。start は検知を 0 とするフレーム、長さは既定の 1
    //! 接触前に開始できない種類は、負の start を 0 にする
    std::size_t AddHitEvent(GL::Level::HitTimeline& timeline,
                            const GL::Level::HitEventValue& value,
                            int start);

    //! @brief row の行の事象を消し、後ろの行を詰める
    //! @return 消した場合 true。row が範囲の外の場合は false
    bool RemoveHitEvent(GL::Level::HitTimeline& timeline, std::size_t row) noexcept;

    //! 事象と命中した下見の全フレームを含む範囲。下見がなければ事象だけ、両方なければ 0〜0
    [[nodiscard]] TimelineFrameRange HitTimelineFrameRange(const GL::Level::HitTimeline& timeline,
                                                           const HitPreviewResult* preview) noexcept;

    //! @brief 下見で row の行の事象が実際に始まったフレームを、検知のフレームを 0 にして返す
    //! @return 始まった順に並ぶ。始まらなかった行は空
    [[nodiscard]] std::vector<int> RowStartFrames(const HitPreviewResult& preview, std::size_t row);

} // namespace NS::Editor
