#pragma once

#include "Editor/Undo/ICommand.h"

#include <memory>
#include <vector>

namespace NS
{
    namespace Editor
    {
        //! 複数の編集を 1 回の Undo にまとめる。実行は追加順、取消は逆順
        class CompositeCommand final : public ICommand
        {
        public:
            explicit CompositeCommand(std::vector<std::unique_ptr<ICommand>> commands) noexcept;

            void Do(IActorSnapshotApplier& target) noexcept override;
            void Undo(IActorSnapshotApplier& target) noexcept override;

            [[nodiscard]] std::size_t EstimatedBytes() const noexcept override;

        private:
            std::vector<std::unique_ptr<ICommand>> m_commands;
        };

        //! 複数の編集を 1 回の Undo にする。1 本なら包まずそのまま返す
        [[nodiscard]] std::unique_ptr<ICommand> MakeUndoUnit(std::vector<std::unique_ptr<ICommand>> commands);
    } // namespace Editor
} // namespace NS
