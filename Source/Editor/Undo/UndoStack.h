#pragma once

/// @file UndoStack.h
/// @brief Command スタック。 std::deque 管理で 200 op / 50 MB の oldest pop_front cap
///
/// @details `Push` 時に redo stack をクリアして編集で履歴を分岐させる
/// `Clear()` は新 level open 時のみ呼び、 mode toggle では呼ばない
/// 50 MB の hard cap は `ICommand::EstimatedBytes()` を合算して判定する

#include "Editor/Undo/ICommand.h"

#include <cstddef>
#include <deque>
#include <memory>

namespace NS::Editor
{

    class UndoStack
    {
    public:
        static constexpr std::size_t kMaxOps = 200;
        static constexpr std::size_t kMaxBytes = 50ull * 1024ull * 1024ull;

        UndoStack() = default;
        ~UndoStack() = default;

        UndoStack(const UndoStack&) = delete;
        UndoStack& operator=(const UndoStack&) = delete;

        /// `cmd->Do(level)` を実行 → m_undo に push_back → m_redo をクリア
        /// 200 op / 50 MB cap に達したら m_undo 先頭から oldest pop
        void Push(std::unique_ptr<ICommand> cmd, NS::Game::Level::LevelData& level) noexcept;

        /// m_undo 末尾の Undo(level) を実行し、 m_redo に移動。 空なら false
        bool Undo(NS::Game::Level::LevelData& level) noexcept;

        /// m_redo 末尾の Do(level) を実行し、 m_undo に戻す。 空なら false
        bool Redo(NS::Game::Level::LevelData& level) noexcept;

        /// 両 stack をクリア。 新 level open 時のみ呼ぶ
        void Clear() noexcept;

        [[nodiscard]] std::size_t UndoSize() const noexcept { return m_undo.size(); }
        [[nodiscard]] std::size_t RedoSize() const noexcept { return m_redo.size(); }
        [[nodiscard]] std::size_t EstimatedBytes() const noexcept { return m_undoBytes + m_redoBytes; }

    private:
        std::deque<std::unique_ptr<ICommand>> m_undo;
        std::deque<std::unique_ptr<ICommand>> m_redo;
        std::size_t m_undoBytes = 0;
        std::size_t m_redoBytes = 0;

        void TrimOldest() noexcept;
    };

} // namespace NS::Editor
