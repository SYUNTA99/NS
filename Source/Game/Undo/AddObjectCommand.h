#pragma once

/// @file AddObjectCommand.h
/// @brief 自由配置オブジェクトを 1 個追加する Command。 Undo で同じ id の要素を削除する

#include "Game/Level/LevelData.h"
#include "Game/Undo/ICommand.h"

#include <cstdint>
#include <optional>

namespace NS::Game::Undo
{

    /// 非 gridAligned な配置物を objects 末尾へ追加する。 grid block と違い cell を持たないため
    /// append 時に採番した識別子で Undo / Redo の対象を再特定する
    class AddObjectCommand final : public ICommand
    {
    public:
        explicit AddObjectCommand(const NS::Game::Level::ObjectInstance& object) noexcept;

        void Do(EditTarget& target) noexcept override;
        void Undo(EditTarget& target) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return sizeof(AddObjectCommand); }

    private:
        NS::Game::Level::ObjectInstance m_object;
        std::optional<std::uint32_t> m_assignedId; // append 時の識別子。 redo で再利用し参照を壊さない
    };

} // namespace NS::Game::Undo
