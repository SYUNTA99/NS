#include "Editor/Undo/CompositeCommand.h"

namespace NS::Editor
{

    CompositeCommand::CompositeCommand(std::vector<std::unique_ptr<ICommand>> commands) noexcept
        : m_commands(std::move(commands))
    {}

    void CompositeCommand::Do(IObjectSnapshotApplier& target) noexcept
    {
        for (auto& command : m_commands)
        {
            if (command)
                command->Do(target);
        }
    }

    void CompositeCommand::Undo(IObjectSnapshotApplier& target) noexcept
    {
        // 後から効かせた分を先に戻さないと、 依存のある編集が食い違う
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
        {
            if (*it)
                (*it)->Undo(target);
        }
    }

    std::size_t CompositeCommand::EstimatedBytes() const noexcept
    {
        std::size_t bytes = sizeof(CompositeCommand);
        for (const auto& command : m_commands)
        {
            if (command)
                bytes += command->EstimatedBytes();
        }
        return bytes;
    }

} // namespace NS::Editor
