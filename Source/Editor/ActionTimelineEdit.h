#pragma once

// 動作のタイムラインの、画面に依らない決まり
// 事象の足し引き・帯の範囲・下見で始まったフレーム
// 出荷ビルドには載らない

#include "Editor/Timeline.h"
#include "NSlib/CoreObject/Action/ActorAction.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace NS
{
    namespace Editor
    {
        struct HitPreviewResult;

        //! 末尾に事象を足し、行番号を返す。start は状態に入った歩を 0 とするフレームで、負は 0 にする。長さは既定の 1
        std::size_t AddActionEvent(ActorAction& action, const HitReactionItem& item, int start);

        //! @brief row の行の事象を消し、後ろの行を詰める
        //! @return 消した場合 true。row が範囲の外の場合は false
        bool RemoveActionEvent(ActorAction& action, std::size_t row) noexcept;

        //! @brief 事象・動作の長さ・下見の全フレームを含む範囲。下見のフレームは、その動作に入った歩を 0 にして数える
        //! @details 動作の長さは 1 以上の時だけ 0〜長さ − 1 を含む。下見が無いか、下見でその動作が走らなかった場合は
        //! 事象と長さだけ。どれも無ければ 0〜0
        [[nodiscard]] TimelineFrameRange ActionFrameRange(const ActorAction& action,
                                                          const HitPreviewResult* preview) noexcept;

        //! @brief 下見の記録で、名前 actionName の動作の 0 フレーム目に当たる frames の添字
        //! @details 途中のフレームから入った動作は 0 フレーム目を通らないので、最初に記録したフレームから戻して出す。
        //! 戻した添字は負になる事がある
        //! @return その動作が走らなかった場合は std::nullopt
        [[nodiscard]] std::optional<int> ActionOriginIndex(const HitPreviewResult& preview,
                                                           std::string_view actionName) noexcept;

        //! @brief 下見で、名前 actionName の動作の row の行の事象が実際に始まったフレームを返す
        //! @return 動作のフレーム (状態に入った歩が 0) で、始まった順に並ぶ。始まらなかった行は空
        [[nodiscard]] std::vector<int> RowStartFrames(const HitPreviewResult& preview,
                                                      std::string_view actionName,
                                                      std::size_t row);
    } // namespace Editor
} // namespace NS
