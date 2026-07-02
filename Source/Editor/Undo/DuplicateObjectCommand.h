#pragma once

/// @file DuplicateObjectCommand.h
/// @brief 選択オブジェクトを全コンポーネント込みで複製する Command。 Undo で複製分を削除する

#include "Editor/Undo/ICommand.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace NS::Editor
{

    /// id で再特定した ObjectInstance を深いコピーで objects 末尾へ足す
    /// @details 複製には新しい識別子を採番し、 redo でも同じ識別子を再利用して参照を壊さない
    class DuplicateObjectCommand final : public ICommand
    {
    public:
        explicit DuplicateObjectCommand(std::uint32_t sourceObjectId) noexcept;

        void Do(NS::Game::Level::EditTarget& target) noexcept override;
        void Undo(NS::Game::Level::EditTarget& target) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return sizeof(DuplicateObjectCommand); }

    private:
        std::uint32_t m_sourceObjectId;
        std::optional<std::uint32_t> m_assignedId;       // 複製の識別子。 redo で再利用する
        std::optional<std::uint32_t> m_assignedObjectId; // 複製の永続 id。 redo で再利用する
    };

} // namespace NS::Editor
