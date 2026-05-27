#include "Game/Editor/BlockRegistry.h"

namespace NS::Game::Editor
{
    const char* GetDisplayName(std::uint16_t blockId) noexcept
    {
        switch (blockId)
        {
        case kBlockIdSolid:
            return "Solid";
        case kBlockIdCoin:
            return "Coin";
        case kBlockIdPowerStar:
            return "Star";
        case kBlockIdSpawn:
            return "Spawn";
        default:
            return "?";
        }
    }

    NS::Core::Color GetBaseColor(std::uint16_t blockId) noexcept
    {
        switch (blockId)
        {
        case kBlockIdSolid:
            return NS::Core::Color{0.70f, 0.70f, 0.75f, 1.0f};
        case kBlockIdCoin:
            return NS::Core::Color{1.00f, 0.85f, 0.20f, 1.0f};
        case kBlockIdPowerStar:
            return NS::Core::Color{1.00f, 0.95f, 0.10f, 1.0f};
        case kBlockIdSpawn:
            return NS::Core::Color{0.30f, 1.00f, 0.30f, 1.0f};
        default:
            return NS::Core::Color{1.0f, 0.0f, 1.0f, 1.0f};
        }
    }
} // namespace NS::Game::Editor
