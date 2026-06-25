#pragma once

/// @file AddComponentCommand.h
/// @brief 対象オブジェクトへコンポーネントを 1 つ追加する Command。 Undo で足した 1 つを取り除く

#include "Editor/Undo/ICommand.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace NS::Editor
{

    /// id で再特定したオブジェクトの components へ型名のみのコンポーネントを足す
    /// @details 同型がすでにあっても重ねて足せる。 足した位置を覚えておき Undo でその 1 つだけを取り除く
    /// 後入れ先出しの undo を前提とし、 Do と Undo の間に同じオブジェクトの components を別操作が変えると位置がずれる
    class AddComponentCommand final : public ICommand
    {
    public:
        AddComponentCommand(std::uint32_t targetObjectId, std::string typeName) noexcept;

        void Do(NS::Game::Level::EditTarget& target) noexcept override;
        void Undo(NS::Game::Level::EditTarget& target) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override;

    private:
        std::uint32_t m_targetObjectId;
        std::string m_typeName;
        std::optional<std::size_t> m_addedIndex; // Do で足した位置。 値があれば Undo がその 1 つを取り除く
    };

} // namespace NS::Editor
