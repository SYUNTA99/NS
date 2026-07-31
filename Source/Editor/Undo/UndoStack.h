#pragma once

#include "Editor/Undo/ICommand.h"
#include "Runtime/Core/NonCopyable.h"

#include <cstdint>
#include <deque>
#include <memory>

namespace NS::Editor
{
    class IObjectSnapshotApplier;

    //! @brief Undo/Redo 履歴管理スタック
    //! @note 上限 (200 操作 / 50 MB) を超えた場合、古い履歴から自動的に破棄する
    //! @note 適用は live 実体を触る IObjectSnapshotApplier 越し。 コマンドは objectId の before/after を往復させる
    class UndoStack : public NS::Core::NonCopyable
    {
    public:
        static constexpr std::size_t k_MaxOps = 200;
        static constexpr std::size_t k_MaxBytes = 50ull * 1024ull * 1024ull;

        UndoStack() = default;
        ~UndoStack() = default;

        //! @brief コマンドを実行し、Undo 履歴へ追加する（Redo 履歴はクリアされる）
        void Push(std::unique_ptr<ICommand> cmd, IObjectSnapshotApplier& target) noexcept;

        //! @brief 既に適用済みの編集を Do を呼ばずに履歴へ積む。 ドラッグ確定など live を先に動かした編集用
        void Record(std::unique_ptr<ICommand> cmd) noexcept;

        //! @brief 最新の Undo 操作を戻し、Redo 履歴へ移動する
        bool Undo(IObjectSnapshotApplier& target) noexcept;

        //! @brief 最新の Redo 操作を実行し、Undo 履歴へ移動する
        bool Redo(IObjectSnapshotApplier& target) noexcept;

        //! @brief 履歴を全消去する。レベル遷移時に使用
        void Clear() noexcept;

        //! @brief 履歴が動いた通算回数
        //! @details 保存時の値と突き合わせて未保存かどうかを見る。 戻して同じ内容に帰っても値は進むので、
        //! 保存済みを未保存と誤る側にだけ倒れる
        [[nodiscard]] std::uint64_t Version() const noexcept { return m_version; }

        [[nodiscard]] std::size_t UndoSize() const noexcept { return m_undo.size(); }
        [[nodiscard]] std::size_t RedoSize() const noexcept { return m_redo.size(); }
        [[nodiscard]] std::size_t EstimatedBytes() const noexcept { return m_undoBytes + m_redoBytes; }

    private:
        std::deque<std::unique_ptr<ICommand>> m_undo; // Undo 待ちの Command 列
        std::deque<std::unique_ptr<ICommand>> m_redo; // Redo 待ちの Command 列
        std::size_t m_undoBytes = 0;                  // Undo 履歴の概算メモリ使用量
        std::size_t m_redoBytes = 0;                  // Redo 履歴の概算メモリ使用量
        std::uint64_t m_version = 0;                  // 履歴が動いた通算回数

        void PushRecorded(std::unique_ptr<ICommand> cmd) noexcept;
        void TrimOldest() noexcept;
    };

} // namespace NS::Editor
