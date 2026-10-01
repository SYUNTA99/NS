#pragma once

#include "Runtime/Object/Component.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
{
    //! @brief 置物の重さ・転がり・壊れ方・飛び方の見た目の調整値の欄を持つ部品
    class MapObjParams : public NS::Obj::Component
    {
    public:
        //! 欄「質量」の値。有限の正でなければ 1
        [[nodiscard]] float Mass() const noexcept
        {
            if (!std::isfinite(m_mass) || m_mass <= 0.0f)
            {
                return 1.0f;
            }
            return m_mass;
        }
        //! 欄「摩擦」の値。有限でなければ 0.6、負は 0
        [[nodiscard]] float Friction() const noexcept
        {
            if (!std::isfinite(m_friction))
            {
                return 0.6f;
            }
            return std::max(m_friction, 0.0f);
        }
        //! 欄「跳ね返り」の値。有限でなければ 0.35、負は 0
        [[nodiscard]] float Restitution() const noexcept
        {
            if (!std::isfinite(m_restitution))
            {
                return 0.35f;
            }
            return std::max(m_restitution, 0.0f);
        }
        [[nodiscard]] float SpinPerSpeed() const noexcept { return m_spinPerSpeed; }
        //! 欄「耐久」の値。有限でなければ 1、負は 0
        [[nodiscard]] float Toughness() const noexcept
        {
            if (!std::isfinite(m_toughness))
            {
                return 1.0f;
            }
            return std::max(m_toughness, 0.0f);
        }
        [[nodiscard]] float RestLifeSeconds() const noexcept { return m_restLifeSeconds; }
        [[nodiscard]] int DebrisCount() const noexcept { return m_debrisCount; }
        [[nodiscard]] float DebrisSpeed() const noexcept { return m_debrisSpeed; }
        [[nodiscard]] float DebrisLifeSeconds() const noexcept { return m_debrisLifeSeconds; }
        [[nodiscard]] float DebrisScale() const noexcept { return m_debrisScale; }
        [[nodiscard]] NS::Core::Vector3 DebrisBaseColor() const noexcept { return m_debrisBaseColor; }
        [[nodiscard]] float MarkProbeDistance() const noexcept { return m_markProbeDistance; }

        NS_REFLECT_BEGIN(MapObjParams, NS::Obj::Component)
        NS_REFLECT_FIELD(m_mass, "質量")
        NS_REFLECT_FIELD(m_friction, "摩擦")
        NS_REFLECT_FIELD(m_restitution, "跳ね返り")
        NS_REFLECT_FIELD(m_spinPerSpeed, "回転の強さ")
        NS_REFLECT_FIELD(m_toughness, "耐久")
        NS_REFLECT_FIELD(m_restLifeSeconds, "止まってから消える秒")
        NS_REFLECT_FIELD(m_debrisCount, "破片の数")
        NS_REFLECT_FIELD(m_debrisSpeed, "破片の速さ")
        NS_REFLECT_FIELD(m_debrisLifeSeconds, "破片の寿命秒")
        NS_REFLECT_FIELD(m_debrisScale, "破片の大きさ")
        NS_REFLECT_FIELD(m_debrisBaseColor, "破片の色")
        NS_REFLECT_FIELD(m_markProbeDistance, "跡の床探しの距離")
        NS_REFLECT_FIELD(m_trailFramesBase, "飛び出しの尾が残るフレーム数の基準")
        NS_REFLECT_FIELD(m_trailFramesPerLaunch, "飛び出しの尾が残るフレーム数の飛ばしの比あたり")
        NS_REFLECT_FIELD(m_landDustBase, "着地の粉の大きさの基準")
        NS_REFLECT_FIELD(m_landDustPerRootMass, "着地の粉の大きさの質量の平方根あたり")
        NS_REFLECT_FIELD(m_landDustPerPower, "着地の粉の大きさの威力あたりの伸び")
        NS_REFLECT_END()

    private:
        friend class LaunchEffects;
        int m_trailFramesBase = 8;
        float m_trailFramesPerLaunch = 4.0f;
        float m_landDustBase = 0.8f;
        float m_landDustPerRootMass = 0.4f;
        float m_landDustPerPower = 0.5f;
        float m_mass = 1.0f;
        float m_friction = 0.6f;
        float m_restitution = 0.35f;
        float m_spinPerSpeed = 0.2f;
        float m_toughness = 1.0f;
        float m_restLifeSeconds = 0.0f;
        int m_debrisCount = 5;
        float m_debrisSpeed = 6.0f;
        float m_debrisLifeSeconds = 8.0f;
        float m_debrisScale = 0.25f;
        NS::Core::Vector3 m_debrisBaseColor{0.35f, 0.32f, 0.30f};
        float m_markProbeDistance = 64.0f;
    };
} // namespace NS::Game::Level
