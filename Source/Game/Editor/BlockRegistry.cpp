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
        case kBlockIdHazard:
            return "Hazard";
        case kBlockIdWater:
            return "Water";
        case kBlockIdDecoration:
            return "Decoration";
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
        case kBlockIdHazard:
            // ダメージを示す警告色 (赤橙系)。 通常 block と一目で区別する。
            return NS::Core::Color{0.95f, 0.30f, 0.15f, 1.0f};
        case kBlockIdWater:
            // 水のシアン系。 透過マテリアルが未配線な間も色で識別可能にする。
            return NS::Core::Color{0.20f, 0.55f, 0.85f, 0.55f};
        case kBlockIdDecoration:
            // 装飾の柔らかい緑系 (草 / 茂みを連想)。 衝突しない目印として淡め。
            return NS::Core::Color{0.50f, 0.75f, 0.40f, 1.0f};
        default:
            return NS::Core::Color{1.0f, 0.0f, 1.0f, 1.0f};
        }
    }

    bool IsSolidBlock(std::uint16_t blockId) noexcept
    {
        // 「cube 形状の固形 block で InstanceBatcher の cube bucket に乗せる対象」 を判定する述語。
        // slope / pole / fence / hazard / water / decoration は専用の Component や mesh を持つため対象外。
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

    bool IsCollidable(std::uint16_t blockId) noexcept
    {
        // hazard は AABB 固形 + 接触ダメージ。 water / decoration は player を素通しさせるため非衝突。
        // pole / fence は capsule との衝突解決はせず climb state machine 経由で扱うため、 物理 collidable
        // ではない。 ここで独立した述語にしておく事で、 CharacterController と各 collider 連携の修正範囲を局所化する。
        return IsSolidBlock(blockId) || IsSlopeBlock(blockId) || IsHazardBlock(blockId);
    }
} // namespace NS::Game::Editor
