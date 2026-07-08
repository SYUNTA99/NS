#pragma once

/// @file ICommand.h
/// @brief Undo/Redo 操作の抽象基底
///
/// @details `Do(level)` で編集操作を実行し、 `Undo(level)` で逆操作する
/// 対象オブジェクトの再特定は ObjectInstance の永続 objectId か cell 座標で行う
/// 派生は座標 + blockId + rotation 等のデータのみを保持し、 描画用ハンドル
/// つまり MeshRendererComponent / Renderer 等は抱えない。 描画は EditorMode が
/// LevelData の変更を観測して再構築する責務で、 所有関係を分離する
/// `EstimatedBytes()` は UndoStack が 50 MB cap を回すためのメモリ使用量見積り

#include "GameCore/Level/LevelData.h"

namespace NS::Editor
{

    class ICommand
    {
    public:
        virtual ~ICommand() = default;

        ICommand(const ICommand&) = delete;
        ICommand& operator=(const ICommand&) = delete;
        ICommand(ICommand&&) = delete;
        ICommand& operator=(ICommand&&) = delete;

        virtual void Do(NS::GameCore::Level::LevelData& level) noexcept = 0;
        virtual void Undo(NS::GameCore::Level::LevelData& level) noexcept = 0;

        [[nodiscard]] virtual std::size_t EstimatedBytes() const noexcept = 0;

    protected:
        ICommand() = default;
    };

} // namespace NS::Editor
