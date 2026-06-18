#include "Game/Undo/UndoStack.h"

#include "Framework/Core/Logger.h"

#include <utility>

namespace NS::Game::Undo
{

    void UndoStack::Push(std::unique_ptr<ICommand> cmd, EditTarget& target) noexcept
    {
        if (!cmd)
            return;

        EnsureIdsConsistent(target);
        cmd->Do(target);
        const std::size_t bytes = cmd->EstimatedBytes();
        m_undoBytes += bytes;
        m_undo.push_back(std::move(cmd));

        // branch on edit: 編集が走った瞬間に redo 履歴は無効になる
        m_redo.clear();
        m_redoBytes = 0;

        TrimOldest();
    }

    bool UndoStack::Undo(EditTarget& target) noexcept
    {
        if (m_undo.empty())
            return false;
        EnsureIdsConsistent(target);
        auto& back = m_undo.back();
        back->Undo(target);
        const std::size_t bytes = back->EstimatedBytes();
        m_undoBytes -= bytes;
        m_redoBytes += bytes;
        m_redo.push_back(std::move(back));
        m_undo.pop_back();
        return true;
    }

    bool UndoStack::Redo(EditTarget& target) noexcept
    {
        if (m_redo.empty())
            return false;
        EnsureIdsConsistent(target);
        auto& back = m_redo.back();
        back->Do(target);
        const std::size_t bytes = back->EstimatedBytes();
        m_redoBytes -= bytes;
        m_undoBytes += bytes;
        m_undo.push_back(std::move(back));
        m_redo.pop_back();
        return true;
    }

    void UndoStack::Clear() noexcept
    {
        m_undo.clear();
        m_redo.clear();
        m_undoBytes = 0;
        m_redoBytes = 0;
    }

    void UndoStack::TrimOldest() noexcept
    {
        while ((m_undo.size() > kMaxOps || m_undoBytes > kMaxBytes) && !m_undo.empty())
        {
            const std::size_t bytes = m_undo.front()->EstimatedBytes();
            m_undoBytes -= bytes;
            m_undo.pop_front();
        }
    }

    void UndoStack::EnsureIdsConsistent(EditTarget& target) noexcept
    {
        if (target.ids.size() == target.level.objects.size())
            return;
        NS_LOG_ERROR(::NS::Core::LogCat::Game,
                     "UndoStack: id 配列が objects と desync ({} != {})、 連番へ再構築する",
                     target.ids.size(),
                     target.level.objects.size());
        ResetEditIds(target);
    }

} // namespace NS::Game::Undo
