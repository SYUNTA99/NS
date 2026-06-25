#pragma once

/// @file RemoveComponentCommand.h
/// @brief 対象オブジェクトから指定型のコンポーネントを除く Command。 除去値を保持し Undo で復元する

#include "Game/Level/LevelData.h"
#include "Editor/Undo/ICommand.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace NS::Editor
{

    /// id で再特定したオブジェクトの components から型名一致を 1 つ除去する
    /// @details 除去した ComponentData を反射値ごと退避し、 Undo で元の添字へ差し戻して順序を保つ
    /// 後入れ先出しの undo を前提とし、 Do と Undo の間に別操作が components を変えると元の添字はずれる
    class RemoveComponentCommand final : public ICommand
    {
    public:
        RemoveComponentCommand(std::uint32_t targetObjectId, std::string typeName) noexcept;

        void Do(NS::Game::Level::EditTarget& target) noexcept override;
        void Undo(NS::Game::Level::EditTarget& target) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override;

    private:
        std::uint32_t m_targetObjectId;
        std::string m_typeName;
        std::optional<NS::Game::Level::ComponentData> m_removed; // 除去した反射値ごとの退避。 Undo 復元で使う
        std::size_t m_removedIndex = 0;                          // 除去前の添字。 Undo で同じ位置へ戻す
    };

} // namespace NS::Editor
