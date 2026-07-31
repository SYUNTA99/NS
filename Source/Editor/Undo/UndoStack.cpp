#include "Editor/Undo/UndoStack.h"

namespace NS::Editor
{

    void UndoStack::Push(std::unique_ptr<ICommand> cmd, IObjectSnapshotApplier& target) noexcept
    {
        if (!cmd)
            return;

        cmd->Do(target);
        PushRecorded(std::move(cmd));
    }

    void UndoStack::Record(std::unique_ptr<ICommand> cmd) noexcept
    {
        if (!cmd)
            return;

        // live は呼出側が既に動かしている。 Do を呼ぶと同じ姿を組み直す無駄が出るので履歴へ積むだけにする
        PushRecorded(std::move(cmd));
    }

    void UndoStack::PushRecorded(std::unique_ptr<ICommand> cmd) noexcept
    {
        ++m_version;
        const std::size_t bytes = cmd->EstimatedBytes();
        m_undoBytes += bytes;
        m_undo.push_back(std::move(cmd));

        // 編集が走った瞬間に Redo 履歴は無効になる
        m_redo.clear();
        m_redoBytes = 0;

        TrimOldest();
    }

    bool UndoStack::Undo(IObjectSnapshotApplier& target) noexcept
    {
        if (m_undo.empty())
            return false;
        ++m_version;
        auto& back = m_undo.back();
        back->Undo(target);
        const std::size_t bytes = back->EstimatedBytes();
        m_undoBytes -= bytes;
        m_redoBytes += bytes;
        m_redo.push_back(std::move(back));
        m_undo.pop_back();
        return true;
    }

    bool UndoStack::Redo(IObjectSnapshotApplier& target) noexcept
    {
        if (m_redo.empty())
            return false;
        ++m_version;
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
        // 読込直後はファイルと同じ内容なので、 版も振り出しへ戻す
        m_version = 0;
    }

    void UndoStack::TrimOldest() noexcept
    {
        while ((m_undo.size() > k_MaxOps || m_undoBytes > k_MaxBytes) && !m_undo.empty())
        {
            const std::size_t bytes = m_undo.front()->EstimatedBytes();
            m_undoBytes -= bytes;
            m_undo.pop_front();
        }
    }

} // namespace NS::Editor
