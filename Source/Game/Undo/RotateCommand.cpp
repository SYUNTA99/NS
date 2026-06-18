#include "Game/Undo/RotateCommand.h"

#include "Game/Level/LevelData.h"

namespace NS::Game::Undo
{
    namespace
    {
        std::uint8_t RotateMod4(std::uint8_t current, std::int8_t delta) noexcept
        {
            // (current + delta) mod 4。 delta は -1 or +1 を想定するが mod 4 で wrap させる
            int32_t r = static_cast<int32_t>(current) + delta;
            r = ((r % 4) + 4) % 4;
            return static_cast<std::uint8_t>(r);
        }
    } // namespace

    RotateCommand::RotateCommand(std::int16_t x, std::int16_t y, std::int16_t z, std::int8_t delta) noexcept
        : m_x(x), m_y(y), m_z(z), m_delta(delta)
    {}

    void RotateCommand::Do(EditTarget& target) noexcept
    {
        NS::Game::Level::LevelData& level = target.level;
        const std::size_t index = NS::Game::Level::FindGridObjectAtCell(level, m_x, m_y, m_z);
        if (index == NS::Game::Level::kNoObjectIndex)
        {
            m_prevRotation.reset();
            return;
        }
        const std::uint8_t step = NS::Game::Level::GridRotationStep(level.objects[index]);
        m_prevRotation = step;
        NS::Game::Level::SetGridRotationStep(level.objects[index], RotateMod4(step, m_delta));
    }

    void RotateCommand::Undo(EditTarget& target) noexcept
    {
        if (!m_prevRotation)
            return;
        NS::Game::Level::LevelData& level = target.level;
        const std::size_t index = NS::Game::Level::FindGridObjectAtCell(level, m_x, m_y, m_z);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        NS::Game::Level::SetGridRotationStep(level.objects[index], *m_prevRotation);
    }

} // namespace NS::Game::Undo
