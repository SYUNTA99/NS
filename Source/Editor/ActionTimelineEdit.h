#pragma once

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

        /// 末尾に事象を足し、行番号を返す。start は状態に入った歩を 0 とするフレーム（負は 0 に補正、長さは既定の 1）
        std::size_t AddActionEvent(ActorAction& action, const HitReactionItem& item, int start);

        /// row の行の事象を消し、後ろの行を詰める。row が範囲外なら false を返す
        bool RemoveActionEvent(ActorAction& action, std::size_t row) noexcept;

        /// 事象・動作の長さ・下見の全フレームを含む範囲（状態に入った歩が 0、該当なしは 0〜0）
        [[nodiscard]] TimelineFrameRange ActionFrameRange(const ActorAction& action,
                                                          const HitPreviewResult* preview) noexcept;

        /// 下見の記録で actionName の 0 フレーム目に当たる frames の添字（負になり得る。未実行なら nullopt）
        [[nodiscard]] std::optional<int> ActionOriginIndex(const HitPreviewResult& preview,
                                                           std::string_view actionName) noexcept;

        /// 下見で actionName の row 行目の事象が実際に始まったフレーム一覧（状態に入った歩が 0、始まった順）
        [[nodiscard]] std::vector<int> RowStartFrames(const HitPreviewResult& preview,
                                                      std::string_view actionName,
                                                      std::size_t row);
    } // namespace Editor
} // namespace NS
