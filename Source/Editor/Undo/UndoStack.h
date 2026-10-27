#pragma once

#include "Editor/Undo/ICommand.h"
#include "NSlib/Core/NonCopyable.h"

#include "NSlib/Core/Types.h"
#include <deque>
#include <memory>

namespace NS::Editor
{
        class IActorSnapshotApplier;

        //! Undo・Redo の履歴。200 操作か 50 MB を超えた分は古い履歴から捨てる
        class UndoStack : public NonCopyable
        {
        public:
            static constexpr std::size_t k_MaxOps = 200;
            static constexpr std::size_t k_MaxBytes = 50ull * 1024ull * 1024ull;

            UndoStack() = default;
            ~UndoStack() = default;

            //! @brief コマンドを実行して Undo 履歴へ積む。Redo 履歴は捨てる
            void Push(std::unique_ptr<ICommand> cmd, IActorSnapshotApplier& target) noexcept;

            //! @brief 既に適用済みの編集を Do を呼ばずに履歴へ積む。ドラッグ確定など実体を先に動かした編集用
            void Record(std::unique_ptr<ICommand> cmd) noexcept;

            //! @brief 最新の Undo 操作を戻し、Redo 履歴へ移動する
            bool Undo(IActorSnapshotApplier& target) noexcept;

            //! @brief 最新の Redo 操作を実行し、Undo 履歴へ移動する
            bool Redo(IActorSnapshotApplier& target) noexcept;

            //! @brief 履歴を全消去する。レベル遷移時に使用
            void Clear() noexcept;

            //! @brief 全部のコマンドが持つアクターを解放し、コマンドは前後の JSON だけで残す
            //! @details シーンを全部作り直す前に呼ぶ。作り直しの後は持っていたアクターを戻せない
            void ReleaseHeldActors() noexcept;

            //! 履歴変更の通算回数。同じ内容へ戻しても進むため、未保存判定は変更ありとみなす側にずれる
            [[nodiscard]] uint64 Version() const noexcept { return m_version; }

            [[nodiscard]] std::size_t UndoSize() const noexcept { return m_undo.size(); }
            [[nodiscard]] std::size_t RedoSize() const noexcept { return m_redo.size(); }
            [[nodiscard]] std::size_t EstimatedBytes() const noexcept { return m_undoBytes + m_redoBytes; }

        private:
            std::deque<std::unique_ptr<ICommand>> m_undo;
            std::deque<std::unique_ptr<ICommand>> m_redo;
            std::size_t m_undoBytes = 0; // Undo 履歴の概算メモリ使用量
            std::size_t m_redoBytes = 0; // Redo 履歴の概算メモリ使用量
            uint64 m_version = 0;

            void PushRecorded(std::unique_ptr<ICommand> cmd) noexcept;
            void TrimOldest() noexcept;
        };
} // namespace NS::Editor
