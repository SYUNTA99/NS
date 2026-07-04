#include "Editor/Undo/PlaceCommand.h"

#include <utility>

namespace NS::Editor
{
    PlaceCommand::PlaceCommand(NS::Game::Level::ObjectInstance prototype,
                               std::int16_t x,
                               std::int16_t y,
                               std::int16_t z,
                               std::uint8_t rotation) noexcept
        : m_x(x), m_y(y), m_z(z), m_rotation(static_cast<std::uint8_t>(rotation & 0x03)),
          m_prototype(std::move(prototype))
    {}

    void PlaceCommand::Do(NS::Game::Level::LevelData& level) noexcept
    {
        const std::size_t index = NS::Game::Level::FindGridObjectAtCell(level, m_x, m_y, m_z);
        // プロトタイプを複製し cell 座標と回転 step だけ焼く。 回転対象外は呼び元が rotation=0 を渡す
        NS::Game::Level::ObjectInstance placed = m_prototype;
        placed.positionX = static_cast<float>(m_x);
        placed.positionY = static_cast<float>(m_y);
        placed.positionZ = static_cast<float>(m_z);
        NS::Game::Level::SetGridRotationStep(placed, m_rotation);
        if (index != NS::Game::Level::kNoObjectIndex)
        {
            // 既存 cell の置換は in-place なので永続 id を同じ場所の物として引き継ぐ
            m_replaced = level.objects[index];
            placed.objectId = level.objects[index].objectId;
            level.objects[index] = placed;
        }
        else
        {
            m_replaced.reset();
            // 永続 id は初回だけ採番し redo で再利用する
            if (!m_assignedObjectId)
                m_assignedObjectId = NS::Game::Level::AllocateObjectId(level);
            placed.objectId = *m_assignedObjectId;
            level.objects.push_back(placed);
        }
    }

    void PlaceCommand::Undo(NS::Game::Level::LevelData& level) noexcept
    {
        const std::size_t index = NS::Game::Level::FindGridObjectAtCell(level, m_x, m_y, m_z);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        if (m_replaced)
        {
            level.objects[index] = *m_replaced;
        }
        else
        {
            level.objects.erase(level.objects.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

} // namespace NS::Editor
