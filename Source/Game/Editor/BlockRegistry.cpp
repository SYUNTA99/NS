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

    bool IsSolidBlock(std::uint16_t blockId) noexcept
    {
        // 04-05 で kBlockIdSlope45、 04-06 で kBlockIdPole / kBlockIdFence、 04-07 で
        // kBlockIdHazard / kBlockIdWater / kBlockIdDecoration を追加する想定。
        // それらは独自 collider component を持つので、 「cube 形状の固形 block」 を判定する本述語の対象外。
        return blockId == kBlockIdSolid;
    }

    bool IsCollidable(std::uint16_t blockId) noexcept
    {
        //  時点で衝突解決対象は固形ブロックのみ。
        // 04-05 で slope (固形扱い)、 04-06 で pole / fence (trigger 寄り)、 04-07 で hazard (固形扱い) /
        // water (非衝突) / decoration (非衝突) を順次追加していく。 ここで独立した述語にしておく事で、
        // CharacterController と StaticColliderComponent 連携の修正範囲を局所化する。
        return IsSolidBlock(blockId);
    }
} // namespace NS::Game::Editor
