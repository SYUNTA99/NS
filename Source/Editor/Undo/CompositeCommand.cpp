#include "Editor/Undo/CompositeCommand.h"

namespace NS::Editor
{
        CompositeCommand::CompositeCommand(std::vector<std::unique_ptr<ICommand>> commands) noexcept
            : m_commands(std::move(commands))
        {}

        void CompositeCommand::Do(IActorSnapshotApplier& target) noexcept
        {
            for (std::unique_ptr<ICommand>& command : m_commands)
            {
                if (command)
                {
                    command->Do(target);
                }
            }
        }

        void CompositeCommand::Undo(IActorSnapshotApplier& target) noexcept
        {
            // 後から効かせた分を先に戻さないと、依存のある編集が食い違う
            for (std::vector<std::unique_ptr<ICommand>>::reverse_iterator it = m_commands.rbegin();
                 it != m_commands.rend();
                 ++it)
            {
                if (*it)
                {
                    (*it)->Undo(target);
                }
            }
        }

        std::size_t CompositeCommand::EstimatedBytes() const noexcept
        {
            std::size_t bytes = sizeof(CompositeCommand);
            for (const std::unique_ptr<ICommand>& command : m_commands)
            {
                if (command)
                {
                    bytes += command->EstimatedBytes();
                }
            }
            return bytes;
        }

        void CompositeCommand::ReleaseHeldActors() noexcept
        {
            for (std::unique_ptr<ICommand>& command : m_commands)
            {
                if (command)
                {
                    command->ReleaseHeldActors();
                }
            }
        }

        std::unique_ptr<ICommand> MakeUndoUnit(std::vector<std::unique_ptr<ICommand>> commands)
        {
            if (commands.size() == 1)
            {
                return std::move(commands.front());
            }
            return std::make_unique<CompositeCommand>(std::move(commands));
        }
} // namespace NS::Editor
