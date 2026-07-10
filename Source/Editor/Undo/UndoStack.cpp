#include "Editor/Undo/UndoStack.h"

namespace NS::Editor
{

    void UndoStack::Push(std::unique_ptr<ICommand> cmd, NS::Scene::SceneData& level) noexcept
    {
        if (!cmd)
            return;

        cmd->Do(level);
        const std::size_t bytes = cmd->EstimatedBytes();
        m_undoBytes += bytes;
        m_undo.push_back(std::move(cmd));

        // 編集が走った瞬間に redo 履歴は無効になる
        m_redo.clear();
        m_redoBytes = 0;

        TrimOldest();
    }

    bool UndoStack::Undo(NS::Scene::SceneData& level) noexcept
    {
        if (m_undo.empty())
            return false;
        auto& back = m_undo.back();
        back->Undo(level);
        const std::size_t bytes = back->EstimatedBytes();
        m_undoBytes -= bytes;
        m_redoBytes += bytes;
        m_redo.push_back(std::move(back));
        m_undo.pop_back();
        return true;
    }

    bool UndoStack::Redo(NS::Scene::SceneData& level) noexcept
    {
        if (m_redo.empty())
            return false;
        auto& back = m_redo.back();
        back->Do(level);
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

} // namespace NS::Editor
