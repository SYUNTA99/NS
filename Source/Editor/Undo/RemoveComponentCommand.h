#pragma once

/// @file RemoveComponentCommand.h
/// @brief 対象オブジェクトから添字指定のコンポーネントを除く Command。 除去値を保持し Undo で復元する

#include "Editor/Undo/ICommand.h"
#include "GameCore/Level/LevelData.h"

namespace NS::Editor
{

    /// 永続 id で再特定したオブジェクトの components から添字 1 つを除去する
    /// @details 添字で狙うので同型が複数あっても選んだ 1 つだけを除ける。 除去した ComponentData を
    /// 反射値ごと退避し、 Undo で元の添字へ差し戻して順序を保つ
    /// 後入れ先出しの undo を前提とし、 Do と Undo の間に別操作が components を変えると元の添字はずれる
    /// 空構成の object は build で不可視・当たり無しのゴーストになるため最後の 1 個は除かない
    class RemoveComponentCommand final : public ICommand
    {
    public:
        RemoveComponentCommand(std::uint32_t targetObjectId, std::size_t componentIndex) noexcept;

        void Do(NS::GameCore::Level::LevelData& level) noexcept override;
        void Undo(NS::GameCore::Level::LevelData& level) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override;

    private:
        std::uint32_t m_targetObjectId;
        std::size_t m_componentIndex;                            // 除去する components の添字
        std::optional<NS::GameCore::Level::ComponentData> m_removed; // 除去した反射値ごとの退避。 Undo 復元で使う
        std::size_t m_removedIndex = 0;                          // 除去前の添字。 Undo で同じ位置へ戻す
    };

} // namespace NS::Editor
