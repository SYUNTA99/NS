#pragma once

#include <string>

#include "Game/Level/HitTier.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Object/SubObject.h"

#include "NSlib/Core/Types.h"
#include <vector>

namespace GL::Level
{
    class MapObj;

    //! @brief 押し飛ばされた配置物の尾と、着地の粉を出す部品
    //! @details 尾は曲線を離れた時に消す。段で色、飛ばしの比で長さ、威力と質量で粉の大きさを決める
    class LaunchEffects : public NS::Obj::SubObject
    {
    public:
        LaunchEffects() noexcept;

        void OnStart() override;

        void OnUpdate() override;

        //! @brief 押し飛ばされた所から尾を出す。前の尾が残っていれば消してから出す
        //! @param[in] tier 当たりの段。尾の色を段ごとの表で引き、中心近くは橙、大きな外れは灰
        //! @param[in] power 最終威力。落ちた所の粉を大きくする
        //! @param[in] launchScale 飛ばしの比。尾を長く残す
        //! @param[in] launchDir 飛ぶ水平の向き。速さが 0 の時の尾の向き
        void BeginTrail(HitTier tier, float power, float launchScale, const NS::Vector3& launchDir);

        //! @brief 尾を消し、落ちた所へ粉を出す
        //! @param[in] position 床に触れた点。世界座標
        //! @param[in] normal 触れた床の法線。粉はこの向きへ少し浮かせて置く
        void NotifyLanding(const NS::Vector3& position, const NS::Vector3& normal);
        //! 出ている尾を消す。粉は出さない
        void CancelTrail();

        //! 出ている尾が残るフレーム数。尾が無ければ 0
        [[nodiscard]] int TrailFrames() const noexcept { return m_trailFrames; }

        //! 落ちた所の粉の大きさ (m)
        [[nodiscard]] float LandDustScale() const noexcept { return m_landDustScale; }

        NS_REFLECT_BEGIN(LaunchEffects, NS::Obj::SubObject)
        NS_REFLECT_FIELD(m_landDustLife, "着地の粉の寿命フレーム")
        NS_REFLECT_FIELD(m_dustRingLift, "着地の粉を浮かせる高さ")
        NS_REFLECT_FIELD(m_launchTrailAsset, "飛び出しの尾の資産")
        NS_REFLECT_FIELD(m_launchLandDustAsset, "着地の粉の資産")
        NS_REFLECT_FIELD(m_landDustAssetRadius, "着地の粉の資産半径")
        NS_REFLECT_FIELD(m_trailFramesBase, "飛び出しの尾が残るフレーム数の基準")
        NS_REFLECT_FIELD(m_trailFramesPerLaunch, "飛び出しの尾が残るフレーム数の飛ばしの比あたり")
        NS_REFLECT_FIELD(m_landDustBase, "着地の粉の大きさの基準")
        NS_REFLECT_FIELD(m_landDustPerRootMass, "着地の粉の大きさの質量の平方根あたり")
        NS_REFLECT_FIELD(m_landDustPerPower, "着地の粉の大きさの威力あたりの伸び")
        NS_REFLECT_END()

    private:
        std::string m_launchTrailAsset = "launch.trail";
        std::string m_launchLandDustAsset = "launch.landDust";
        float m_landDustAssetRadius = 1.2f;
        int m_landDustLife = 24;
        //! 床の法線の向きへ浮かせる。単位はメートル
        float m_dustRingLift = 0.3f;
        int m_trailFramesBase = 8;
        float m_trailFramesPerLaunch = 4.0f;
        float m_landDustBase = 0.8f;
        float m_landDustPerRootMass = 0.4f;
        float m_landDustPerPower = 0.5f;
        // 尾を消し、床に落ちていればその場へ粉を出す
        void EndTrail();

        MapObj* m_body = nullptr;
        NS::uint32 m_trail = 0;   // 飛び出しの尾。消したら 0
        int m_trailStartStep = 0; // 尾を出したフレーム。このフレームは出した姿のまま
        int m_trailFrames = 0;
        float m_trailScale = 1.0f; // 尾の再生の大きさ。自分の直径 (m)
        NS::Vector3 m_launchDir{1.0f, 0.0f, 0.0f};
        float m_landDustScale = 0.0f; // 落ちた所の粉の大きさ (m)
    };
} // namespace GL::Level
