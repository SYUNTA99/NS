#pragma once

/// @file ICommand.h
/// @brief Undo/Redo Command パターンの抽象基底
///
/// @details `Do(target)` で編集操作を実行し、 `Undo(target)` で逆操作する
/// 派生は座標 + blockId + rotation 等のデータのみを保持し、 描画用 entity
/// (MeshRendererComponent / Renderer ハンドル等) を抱えない。 描画は EditorMode が
/// LevelData の変更を観測して再構築する責務 (Command と描画の所有関係分離)
/// `EstimatedBytes()` は UndoStack が 50 MB cap を回すための memory accounting

#include "Game/Undo/EditTarget.h"

#include <cstddef>

namespace NS::Game::Undo
{

    class ICommand
    {
    public:
        virtual ~ICommand() = default;

        ICommand(const ICommand&) = delete;
        ICommand& operator=(const ICommand&) = delete;
        ICommand(ICommand&&) = delete;
        ICommand& operator=(ICommand&&) = delete;

        virtual void Do(EditTarget& target) noexcept = 0;
        virtual void Undo(EditTarget& target) noexcept = 0;

        [[nodiscard]] virtual std::size_t EstimatedBytes() const noexcept = 0;

    protected:
        ICommand() = default;
    };

} // namespace NS::Game::Undo
