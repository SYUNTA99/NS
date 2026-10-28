#pragma once

#include "NSlib/CoreObject/Asset/EffectAsset.h"
#include "NSlib/CoreObject/Object.h"

namespace GL::Level
{
    class LaunchEffectSettings : public NS::Object
    {
    public:
        int m_landDustLife = 24;
        //! 床の法線の向きへ浮かせる。単位はメートル
        float m_dustRingLift = 0.3f;
        NS::EffectAsset* m_launchTrailAsset = nullptr;
        NS::EffectAsset* m_launchLandDustAsset = nullptr;
        float m_landDustAssetRadius = 1.2f;
        int m_trailFramesBase = 8;
        float m_trailFramesPerLaunch = 4.0f;
        float m_landDustBase = 0.8f;
        float m_landDustPerRootMass = 0.4f;
        float m_landDustPerPower = 0.5f;

        NS_REFLECT_BEGIN(LaunchEffectSettings, NS::Object)
        NS_FIELD(m_landDustLife, "着地の粉の寿命フレーム")
        NS_FIELD(m_dustRingLift, "着地の粉を浮かせる高さ")
        NS_FIELD(m_launchTrailAsset, "飛び出しの尾の資産")
        NS_FIELD(m_launchLandDustAsset, "着地の粉の資産")
        NS_FIELD(m_landDustAssetRadius, "着地の粉の資産半径")
        NS_FIELD(m_trailFramesBase, "飛び出しの尾が残るフレーム数の基準")
        NS_FIELD(m_trailFramesPerLaunch, "飛び出しの尾が残るフレーム数の飛ばしの比あたり")
        NS_FIELD(m_landDustBase, "着地の粉の大きさの基準")
        NS_FIELD(m_landDustPerRootMass, "着地の粉の大きさの質量の平方根あたり")
        NS_FIELD(m_landDustPerPower, "着地の粉の大きさの威力あたりの伸び")
        NS_REFLECT_END()
    };
} // namespace GL::Level
