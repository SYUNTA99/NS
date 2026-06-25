#pragma once

/// @file TransformCommand.h
/// @brief 識別子で指す 1 オブジェクトを before → after の ObjectInstance スナップショットで置換する Command
///
/// @details 自由オブジェクトのギズモ変形 / パネル編集 / grid→free 昇格を 1 単位で undo するために使う
/// 位置・回転・スケール・flags・materialIndex を丸ごと持つため昇格 (flags 変化) も同じ型で表せる
/// 対象は識別子 (EditTarget::ids) で再特定するので、間に Place / Delete で添字がずれても追従する

#include "Game/Level/LevelData.h"
#include "Editor/Undo/ICommand.h"

#include <cstddef>
#include <cstdint>

namespace NS::Editor
{

    class TransformCommand final : public ICommand
    {
    public:
        /// `id` の objects を `after` へ置く Command。 Undo で `before` へ戻す
        TransformCommand(std::uint32_t id,
                         const NS::Game::Level::ObjectInstance& before,
                         const NS::Game::Level::ObjectInstance& after) noexcept;

        void Do(NS::Game::Level::EditTarget& target) noexcept override;
        void Undo(NS::Game::Level::EditTarget& target) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return sizeof(TransformCommand); }

    private:
        std::uint32_t m_id;
        NS::Game::Level::ObjectInstance m_before;
        NS::Game::Level::ObjectInstance m_after;
    };

} // namespace NS::Editor
