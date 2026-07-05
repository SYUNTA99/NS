#include "GameCore/Blocks/BlockRegistry.h"

namespace NS::GameCore::Blocks
{
    float BlockRotationToYaw(std::uint8_t rotation) noexcept
    {
        constexpr float kTwoPi = 6.2831853071795864769f;
        return static_cast<float>(rotation) * (kTwoPi / static_cast<float>(kBlockRotationSteps));
    }
} // namespace NS::GameCore::Blocks
