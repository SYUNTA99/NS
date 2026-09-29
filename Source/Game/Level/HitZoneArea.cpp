#include "Game/Level/HitZoneArea.h"

#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        // 非数と無限は割合として意味を持たないので 0。それ以外は lower〜upper へ丸める
        [[nodiscard]] float SanitizeShare(float value, float lower, float upper) noexcept
        {
            if (!std::isfinite(value))
            {
                return 0.0f;
            }
            return std::clamp(value, lower, upper);
        }
    } // namespace

    bool HitZoneArea::Contains(float u, float v) const noexcept
    {
        // 広さ 0 で割らないよう先に外す。縁ちょうどは入らない
        if (!(m_width > 0.0f) || !(m_height > 0.0f))
        {
            return false;
        }
        const float x = (u - m_centerU) / m_width;
        const float y = (v - m_centerV) / m_height;
        if (m_round)
        {
            return x * x + y * y < 1.0f;
        }
        return std::abs(x) < 1.0f && std::abs(y) < 1.0f;
    }

    void HitZoneArea::SetWidth(float width) noexcept
    {
        m_width = SanitizeShare(width, 0.0f, 1.0f);
    }

    void HitZoneArea::SetHeight(float height) noexcept
    {
        m_height = SanitizeShare(height, 0.0f, 1.0f);
    }

    void HitZoneArea::SetCenterU(float u) noexcept
    {
        m_centerU = SanitizeShare(u, -1.0f, 1.0f);
    }

    void HitZoneArea::SetCenterV(float v) noexcept
    {
        m_centerV = SanitizeShare(v, -1.0f, 1.0f);
    }

    void HitZoneArea::SetPowerScale(float scale) noexcept
    {
        if (!std::isfinite(scale))
        {
            m_powerScale = 0.0f;
            return;
        }
        m_powerScale = std::max(scale, 0.0f);
    }

    NS_CLASS(HitZoneArea)
} // namespace NS::Game::Level
