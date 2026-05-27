#include "Game/Undo/RotateCommand.h"

#include "Game/Level/LevelData.h"

#include <algorithm>

namespace NS::Game::Undo
{
    namespace
    {
        auto FindCell(NS::Game::Level::LevelData& level, std::int16_t x, std::int16_t y, std::int16_t z)
        {
            return std::find_if(level.blocks.begin(), level.blocks.end(), [x, y, z](const auto& b) {
                return b.x == x && b.y == y && b.z == z;
            });
        }

        std::uint8_t RotateMod4(std::uint8_t current, std::int8_t delta) noexcept
        {
            // (current + delta) mod 4。 delta は -1 or +1 を想定するが mod 4 で wrap させる。
            int32_t r = static_cast<int32_t>(current) + delta;
            r = ((r % 4) + 4) % 4;
            return static_cast<std::uint8_t>(r);
        }
    } // namespace

    RotateCommand::RotateCommand(std::int16_t x, std::int16_t y, std::int16_t z, std::int8_t delta) noexcept
        : m_x(x), m_y(y), m_z(z), m_delta(delta)
    {}

    void RotateCommand::Do(NS::Game::Level::LevelData& level) noexcept
    {
        auto it = FindCell(level, m_x, m_y, m_z);
        if (it == level.blocks.end())
        {
            m_prevRotation.reset();
            return;
        }
        m_prevRotation = it->rotation;
        it->rotation = RotateMod4(it->rotation, m_delta);
    }

    void RotateCommand::Undo(NS::Game::Level::LevelData& level) noexcept
    {
        if (!m_prevRotation)
            return;
        auto it = FindCell(level, m_x, m_y, m_z);
        if (it == level.blocks.end())
            return;
        it->rotation = *m_prevRotation;
    }

} // namespace NS::Game::Undo
