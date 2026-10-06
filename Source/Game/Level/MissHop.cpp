#include "Game/Level/MissHop.h"

#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        constexpr float k_Pi = 3.14159265358979f;

        // 種と番号を混ぜて 0〜1 の値にする。整数の混ぜ方は種の近い当たりでも値が散る形
        [[nodiscard]] float Hash01(std::uint32_t seed, std::uint32_t salt) noexcept
        {
            std::uint32_t x = seed * 0x9E3779B1u + salt * 0x85EBCA77u;
            x ^= x >> 16;
            x *= 0x7FEB352Du;
            x ^= x >> 15;
            x *= 0x846CA68Bu;
            x ^= x >> 16;
            return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
        }
    } // namespace

    NS::Vector3 MissHopVelocity(const NS::Vector3& velocity,
                                      const NS::Vector3& up,
                                      const MissHopDesc& desc,
                                      std::uint32_t seed,
                                      int hopIndex) noexcept
    {
        const NS::Vector3 horizontal = velocity - up * velocity.Dot(up);
        const float speed = horizontal.Length();
        if (!(speed > 0.0001f))
        {
            return NS::Vector3{0.0f, 0.0f, 0.0f};
        }
        const std::uint32_t index = static_cast<std::uint32_t>(hopIndex);
        const float turn = (Hash01(seed, index * 2u) * 2.0f - 1.0f) * desc.turnDegrees * (k_Pi / 180.0f);
        const float lift = 0.5f + 0.5f * Hash01(seed, index * 2u + 1u);
        const NS::Vector3 forward = horizontal / speed;
        const NS::Vector3 side = up.Cross(forward);
        const NS::Vector3 turned = forward * std::cos(turn) + side * std::sin(turn);
        const float kept = speed * desc.keep;
        return turned * kept + up * (kept * desc.heightRatio * lift);
    }
} // namespace NS::Game::Level
