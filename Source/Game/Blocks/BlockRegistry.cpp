#include "Game/Blocks/BlockRegistry.h"

namespace NS::Game::Blocks
{
    float BlockRotationToYaw(std::uint8_t rotation) noexcept
    {
        constexpr float kTwoPi = 6.2831853071795864769f;
        return static_cast<float>(rotation) * (kTwoPi / static_cast<float>(kBlockRotationSteps));
    }

    NS::Math::Color GetBaseColor(std::uint16_t blockId) noexcept
    {
        switch (blockId)
        {
        case kBlockIdSolid:
            return NS::Math::Color{0.70f, 0.70f, 0.75f, 1.0f};
        case kBlockIdCoin:
            return NS::Math::Color{1.00f, 0.85f, 0.20f, 1.0f};
        case kBlockIdPowerStar:
            return NS::Math::Color{1.00f, 0.95f, 0.10f, 1.0f};
        case kBlockIdSpawn:
            return NS::Math::Color{0.30f, 1.00f, 0.30f, 1.0f};
        case kBlockIdSlope45:
        case kBlockIdSlope30:
        case kBlockIdSlope22:
        case kBlockIdSlope15:
            // solid と同系色だが少しウォーム寄りで識別できるようにする (仮)
            return NS::Math::Color{0.78f, 0.65f, 0.50f, 1.0f};
        case kBlockIdPole:
            // 木製ポールを意識した茶色系
            return NS::Math::Color{0.55f, 0.40f, 0.25f, 1.0f};
        case kBlockIdHazard:
            // ダメージを示す警告色 (赤橙系)。 通常 block と一目で区別する
            return NS::Math::Color{0.95f, 0.30f, 0.15f, 1.0f};
        case kBlockIdWater:
            // 水のシアン系。 透過マテリアルが未配線な間も色で識別可能にする
            return NS::Math::Color{0.20f, 0.55f, 0.85f, 0.55f};
        case kBlockIdDecoration:
            // 装飾の柔らかい緑系 (草 / 茂みを連想)。 衝突しない目印として淡め
            return NS::Math::Color{0.50f, 0.75f, 0.40f, 1.0f};
        default:
            return NS::Math::Color{1.0f, 0.0f, 1.0f, 1.0f};
        }
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

    std::uint16_t NextSlopeBlock(std::uint16_t blockId) noexcept
    {
        switch (blockId)
        {
        case kBlockIdSlope45:
            return kBlockIdSlope30;
        case kBlockIdSlope30:
            return kBlockIdSlope22;
        case kBlockIdSlope22:
            return kBlockIdSlope15;
        case kBlockIdSlope15:
            return kBlockIdSlope45;
        default:
            return blockId;
        }
    }

    bool IsPoleBlock(std::uint16_t blockId) noexcept
    {
        return blockId == kBlockIdPole;
    }

    bool IsHazardBlock(std::uint16_t blockId) noexcept
    {
        return blockId == kBlockIdHazard;
    }

    bool IsWaterBlock(std::uint16_t blockId) noexcept
    {
        return blockId == kBlockIdWater;
    }

    bool IsDecorationBlock(std::uint16_t blockId) noexcept
    {
        return blockId == kBlockIdDecoration;
    }
} // namespace NS::Game::Blocks
