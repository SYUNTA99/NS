#pragma once

/// @file SetObjectComponentsCommand.h
/// @brief 対象オブジェクトの components 一覧を丸ごと置き換える Command。 Undo で元の一覧へ戻す

#include "Editor/Undo/ICommand.h"
#include "GameCore/Level/LevelData.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace NS::Editor
{

    /// 永続 id で再特定したオブジェクトの components を新しい一覧へ置き換える
    /// @details 複数の component 変更を 1 つの undo 単位へまとめる時に使う
    /// Do で旧一覧を退避し、 Undo で差し戻す
    class SetObjectComponentsCommand final : public ICommand
    {
    public:
        SetObjectComponentsCommand(std::uint32_t targetObjectId,
                                   std::vector<NS::GameCore::Level::ComponentData> newComponents) noexcept;

        void Do(NS::GameCore::Level::LevelData& level) noexcept override;
        void Undo(NS::GameCore::Level::LevelData& level) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override;

    private:
        std::uint32_t m_targetObjectId;
        std::vector<NS::GameCore::Level::ComponentData> m_newComponents;
        std::vector<NS::GameCore::Level::ComponentData> m_oldComponents; // Do で退避した置換前の一覧
    };

} // namespace NS::Editor
