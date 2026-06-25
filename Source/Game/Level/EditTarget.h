#pragma once

/// @file EditTarget.h
/// @brief Command が編集する対象の非所有 view。 LevelData.objects と同長の transient id を束ねる
///
/// @details `ids` は `objects` と 1:1 対応の **セッション限定** 識別子で `.nslvl` には保存しない
/// 自由オブジェクトを undo 履歴から再特定するために使う (grid block は cell 座標で再特定するため
/// id を参照しない)。 `objects` の追加 / 削除と `ids` は常に lockstep で維持する不変条件を持ち、
/// objects を全置換 (load / 既定生成 / migrate) した直後は `ResetEditIds` で連番へ復元する

#include "Game/Level/LevelData.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace NS::Game::Level
{
    /// 識別子の「該当無し」を表す番兵
    inline constexpr std::uint32_t kInvalidObjectId = static_cast<std::uint32_t>(-1);

    /// 編集対象の非所有 view。 level / ids / nextId への参照を束ねるだけで所有はしない
    struct EditTarget
    {
        NS::Game::Level::LevelData& level;
        std::vector<std::uint32_t>& ids; ///< level.objects と同長、 各要素のセッション識別子
        std::uint32_t& nextId;           ///< 次に採番する識別子 (単調増加、 削除でも巻き戻さない)
    };

    /// ids を objects と同サイズの連番 [0..N) へ再構築し nextId=N へ戻す
    /// @details objects を全置換した直後に呼び lockstep を回復する。 既存 id 対応は失われるため
    /// undo 履歴の clear と対で使う
    inline void ResetEditIds(EditTarget& target) noexcept
    {
        const std::size_t count = target.level.objects.size();
        target.ids.resize(count);
        for (std::size_t i = 0; i < count; ++i)
            target.ids[i] = static_cast<std::uint32_t>(i);
        target.nextId = static_cast<std::uint32_t>(count);
    }

    /// 識別子 `id` を持つ objects の添字。 無ければ kNoObjectIndex
    [[nodiscard]] inline std::size_t IndexOfId(const EditTarget& target, std::uint32_t id) noexcept
    {
        for (std::size_t i = 0; i < target.ids.size(); ++i)
        {
            if (target.ids[i] == id)
                return i;
        }
        return NS::Game::Level::kNoObjectIndex;
    }

    /// 添字 `index` の識別子。 範囲外なら kInvalidObjectId
    [[nodiscard]] inline std::uint32_t IdAt(const EditTarget& target, std::size_t index) noexcept
    {
        return index < target.ids.size() ? target.ids[index] : kInvalidObjectId;
    }

} // namespace NS::Game::Level
