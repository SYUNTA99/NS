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
        case kBlockIdSlope45:
            return "Slope 45";
        case kBlockIdSlope30:
            return "Slope 30";
        case kBlockIdSlope22:
            return "Slope 22.5";
        case kBlockIdSlope15:
            return "Slope 15";
        case kBlockIdPole:
            return "Pole";
        case kBlockIdFence:
            return "Fence";
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
        case kBlockIdSlope45:
        case kBlockIdSlope30:
        case kBlockIdSlope22:
        case kBlockIdSlope15:
            // solid と同系色だが少しウォーム寄りで識別できるようにする (placeholder)。
            return NS::Core::Color{0.78f, 0.65f, 0.50f, 1.0f};
        case kBlockIdPole:
            // 木製ポールを意識した茶色系。 fence と区別するため少し明るめ。
            return NS::Core::Color{0.55f, 0.40f, 0.25f, 1.0f};
        case kBlockIdFence:
            // 金網を意識した灰色系。 pole よりはっきり暗く。
            return NS::Core::Color{0.45f, 0.45f, 0.50f, 1.0f};
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

    bool IsSlopeBlock(std::uint16_t blockId) noexcept
    {
        return blockId == kBlockIdSlope45 || blockId == kBlockIdSlope30 || blockId == kBlockIdSlope22 ||
               blockId == kBlockIdSlope15;
    }

    float GetSlopeAngleDegrees(std::uint16_t blockId) noexcept
    {
        switch (blockId)
        {
        case kBlockIdSlope45:
            return 45.0f;
        case kBlockIdSlope30:
            return 30.0f;
        case kBlockIdSlope22:
            return 22.5f;
        case kBlockIdSlope15:
            return 15.0f;
        default:
            return 0.0f;
        }
    }

    bool IsPoleBlock(std::uint16_t blockId) noexcept
    {
        return blockId == kBlockIdPole;
    }

    bool IsFenceBlock(std::uint16_t blockId) noexcept
    {
        return blockId == kBlockIdFence;
    }

    bool IsCollidable(std::uint16_t blockId) noexcept
    {
        // 04-07 で hazard (固形扱い) / water (非衝突) / decoration (非衝突) を順次追加していく。
        // pole / fence は capsule との衝突解決はせず climb state machine 経由で扱うため、 物理 collidable
        // ではない。 ここで独立した述語にしておく事で、 CharacterController と各 collider 連携の修正範囲を局所化する。
        return IsSolidBlock(blockId) || IsSlopeBlock(blockId);
    }
} // namespace NS::Game::Editor
