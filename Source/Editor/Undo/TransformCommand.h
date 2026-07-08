#pragma once

/// @file TransformCommand.h
/// @brief 永続 id で指す 1 オブジェクトを before → after の ObjectInstance スナップショットで置換する Command
///
/// @details 自由オブジェクトのギズモ変形 / パネル編集 / grid→free 昇格を 1 単位で undo するために使う
/// 位置・回転・スケール・flags・materialIndex を丸ごと持つため flags 変化の昇格も同じ型で表せる
/// 対象は ObjectInstance の永続 objectId で再特定するので、間に Place / Delete で添字がずれても追従する

#include "Editor/Undo/ICommand.h"
#include "Game/Level/LevelData.h"

namespace NS::Editor
{

    class TransformCommand final : public ICommand
    {
    public:
        /// 永続 id `id` の objects を `after` へ置く Command。 Undo で `before` へ戻す
        TransformCommand(std::uint32_t id,
                         const NS::Game::Level::ObjectInstance& before,
                         const NS::Game::Level::ObjectInstance& after) noexcept;

        void Do(NS::Game::Level::LevelData& level) noexcept override;
        void Undo(NS::Game::Level::LevelData& level) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override
        {
            return sizeof(TransformCommand) + NS::Game::Level::EstimatedHeapBytes(m_before) +
                   NS::Game::Level::EstimatedHeapBytes(m_after);
        }

    private:
        std::uint32_t m_id;
        NS::Game::Level::ObjectInstance m_before;
        NS::Game::Level::ObjectInstance m_after;
    };

} // namespace NS::Editor
