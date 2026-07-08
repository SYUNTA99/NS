#pragma once

/// @file AddObjectCommand.h
/// @brief 自由配置オブジェクトを 1 個追加する Command。 Undo で同じ永続 id の要素を削除する

#include "Editor/Undo/ICommand.h"
#include "Game/Level/LevelData.h"

namespace NS::Editor
{

    /// 配置物を objects 末尾へ追加する。 cell に紐付かない自由配置なので
    /// 初回 Do で採番した永続 objectId で Undo / Redo の対象を再特定する
    class AddObjectCommand final : public ICommand
    {
    public:
        explicit AddObjectCommand(const NS::Game::Level::ObjectInstance& object) noexcept;

        void Do(NS::Game::Level::LevelData& level) noexcept override;
        void Undo(NS::Game::Level::LevelData& level) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override
        {
            return sizeof(AddObjectCommand) + NS::Game::Level::EstimatedHeapBytes(m_object);
        }

    private:
        NS::Game::Level::ObjectInstance m_object;
    };

} // namespace NS::Editor
