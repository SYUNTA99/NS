#pragma once

#include "Game/Level/MissHop.h"
#include "NSlib/Object/SubObject.h"

#include <algorithm>
#include <cmath>

namespace GL::Level
{
    //! @brief 置物の重さ・転がり・壊れ方・飛び方の見た目の調整値の欄を持つ部品
    class MapObjParams : public NS::Obj::SubObject
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
        [[nodiscard]] float FloorDot() const noexcept { return std::clamp(m_floorDot, NS::k_Epsilon, 1.0f); }
        [[nodiscard]] int DebrisCount() const noexcept { return m_debrisCount; }
        [[nodiscard]] float DebrisSpeed() const noexcept { return m_debrisSpeed; }
        [[nodiscard]] float DebrisLifeSeconds() const noexcept { return m_debrisLifeSeconds; }
        [[nodiscard]] float DebrisScale() const noexcept { return m_debrisScale; }
        [[nodiscard]] NS::Vector3 DebrisBaseColor() const noexcept { return m_debrisBaseColor; }
        [[nodiscard]] float MarkProbeDistance() const noexcept { return m_markProbeDistance; }
        //! 欄「外れで跳ねる回数」の値。負は 0
        [[nodiscard]] int MissHopCount() const noexcept { return std::max(m_missHopCount, 0); }
        //! 外れで着地した後の跳ね方の欄をまとめた値
        [[nodiscard]] MissHopDesc MissHop() const noexcept
        {
            return MissHopDesc{
                .heightRatio = m_missHopHeightRatio, .turnDegrees = m_missHopTurnDegrees, .keep = m_missHopKeep};
        }

        [[nodiscard]] float ContactSkin() const noexcept { return std::max(m_contactSkin, 0.0f); }
        [[nodiscard]] float StopSpeed() const noexcept { return std::max(m_stopSpeed, 0.0f); }
        [[nodiscard]] int MaxContacts() const noexcept { return std::max(m_maxContacts, 1); }

        NS_REFLECT_BEGIN(MapObjParams, NS::Obj::SubObject)
        NS_REFLECT_FIELD(m_mass, "質量")
        NS_REFLECT_FIELD(m_friction, "摩擦")
        NS_REFLECT_FIELD(m_restitution, "跳ね返り")
        NS_REFLECT_FIELD(m_spinPerSpeed, "回転の強さ")
        NS_REFLECT_FIELD(m_toughness, "耐久")
        NS_REFLECT_FIELD(m_restLifeSeconds, "止まってから消える秒")
        NS_REFLECT_FIELD(m_floorDot, "床とみなす法線の上向き成分")
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
        NS_REFLECT_FIELD(m_missHopCount, "外れで跳ねる回数")
        NS_REFLECT_FIELD(m_missHopHeightRatio, "外れで跳ねる高さの割合")
        NS_REFLECT_FIELD(m_missHopTurnDegrees, "外れで跳ねる向きのぶれの角度")
        NS_REFLECT_FIELD(m_missHopKeep, "外れで跳ねるたびに残る速さの割合")
        NS_REFLECT_FIELD(m_contactSkin, "接触の余白")
        NS_REFLECT_FIELD(m_stopSpeed, "停止とみなす速さ")
        NS_REFLECT_FIELD(m_maxContacts, "接触を解く回数")
        NS_REFLECT_END()

    private:
        float m_contactSkin = 0.001f;
        float m_stopSpeed = 0.01f;
        int m_maxContacts = 4;
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
        //! 法線の上向き成分。既定は 45 度までの面を床とする
        float m_floorDot = 0.7071f;
        int m_debrisCount = 5;
        float m_debrisSpeed = 6.0f;
        float m_debrisLifeSeconds = 8.0f;
        float m_debrisScale = 0.25f;
        NS::Vector3 m_debrisBaseColor{0.35f, 0.32f, 0.30f};
        float m_markProbeDistance = 64.0f;
        // 外れで飛ばされた置物は、着地で 1 回だけ小さく向きを変えて跳ね、転がって止まる。真ん中のまっすぐ飛ぶ弧と
        // 違って、力がまともに入らずかすめた事を相手の動きで見せる。何度も跳ねると目を引いて派手になるので 1 回、
        // 跳ねの高さは水平の速さの 0.35 倍まで、向きは左右 30 度までぶらし、速さは跳ねると 7 割に落とす
        int m_missHopCount = 1;
        float m_missHopHeightRatio = 0.35f;
        float m_missHopTurnDegrees = 30.0f;
        float m_missHopKeep = 0.7f;
    };
} // namespace GL::Level
