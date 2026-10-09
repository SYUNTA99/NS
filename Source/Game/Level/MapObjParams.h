#pragma once

#include "Game/Level/LaunchArc.h"
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
        [[nodiscard]] LaunchShape Launch() const noexcept
        {
            return LaunchShape{.apexHeight = m_launchApexHeight,
                               .riseGravity = m_launchRiseGravity,
                               .fallGravityScale = m_launchFallGravityScale,
                               .apexBandSpeed = m_launchApexBandSpeed,
                               .apexBandGravityScale = m_launchApexBandGravityScale,
                               .missHeightRatio = m_missLaunchHeightRatio,
                               .missDistanceRatio = m_missLaunchDistanceRatio};
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
        NS_REFLECT_FIELD(m_launchApexHeight, "押し飛ばしの高さ")
        NS_REFLECT_FIELD(m_launchRiseGravity, "押し飛ばしの上昇重力")
        NS_REFLECT_FIELD(m_launchFallGravityScale, "下りの速さの倍率")
        NS_REFLECT_FIELD(m_launchApexBandSpeed, "頂点の帯の縦速度")
        NS_REFLECT_FIELD(m_launchApexBandGravityScale, "頂点の帯の重力倍率")
        NS_REFLECT_FIELD(m_missLaunchHeightRatio, "外れで飛ばす相手の弧の高さの割合")
        NS_REFLECT_FIELD(m_missLaunchDistanceRatio, "外れで飛ばす相手の距離の割合")
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
        float m_launchApexHeight = 2.0f;
        float m_launchRiseGravity = 25.0f;
        float m_launchFallGravityScale = 1.4f;
        float m_launchApexBandSpeed = 1.0f;
        float m_launchApexBandGravityScale = 0.5f;
        // 外れの相手は低く飛ばす。真ん中と同じ角度の弧だと弾き飛ばしに見える
        // 距離と同じ押し込む成分の 2 乗の縮みの上から、高さへ掛ける割合
        float m_missLaunchHeightRatio = 0.35f;
        // 外れの相手は触れた所から少しずれるだけにする
        // 押し込む成分の 2 乗の縮みの上から、距離へ掛ける割合
        float m_missLaunchDistanceRatio = 0.1f;
        // 外れで飛ばされた置物は、着地で 1 回だけ小さく向きを変えて跳ね、転がって止まる。真ん中のまっすぐ飛ぶ弧と
        // 違って、力がまともに入らずかすめた事を相手の動きで見せる。何度も跳ねると目を引いて派手になるので 1 回、
        // 跳ねの高さは水平の速さの 0.35 倍まで、向きは左右 30 度までぶらし、速さは跳ねると 7 割に落とす
        int m_missHopCount = 1;
        float m_missHopHeightRatio = 0.35f;
        float m_missHopTurnDegrees = 30.0f;
        float m_missHopKeep = 0.7f;
    };
} // namespace GL::Level
