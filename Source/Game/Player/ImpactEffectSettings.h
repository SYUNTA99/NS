#pragma once

#include "NSlib/CoreObject/Asset/EffectAsset.h"
#include "NSlib/CoreObject/Object.h"

namespace GL::Player
{
    class ImpactEffectSettings : public NS::Object
    {
    public:
        float m_coreDiameterBase = 0.3f;
        float m_coreDiameterPerPower = 0.2f;
        float m_coreDiameterMax = 0.7f;
        float m_coreBirthScale = 0.5f;
        float m_streakLengthBase = 4.0f;
        float m_streakLengthPerPower = 2.0f;
        float m_streakEndThickness = 0.3f;
        float m_ringRadiusBase = 0.5f;
        float m_ringRadiusPerPower = 0.6f;
        float m_ringStartRadius = 0.3f;
        float m_ringFaceCamera = 1.0f;
        int m_sparkCountMin = 10;
        int m_sparkCountMax = 30;
        float m_sparkSpeedBase = 6.0f;
        float m_sparkSpeedPerLaunch = 3.0f;
        int m_wideSparkCount = 16;
        float m_wideSparkSpeed = 4.0f;
        // 外れの火花全体の倍率。粒・散る範囲・速さが一緒に伸びる
        float m_wideSparkScale = 3.0f;
        float m_emberShare = 7.0f;
        float m_glowDiameterBase = 2.0f;
        float m_glowDiameterPerPower = 1.6f;
        int m_recoilCount = 8;
        // 外れには弾き返された事を表す線を出さない
        int m_wideRecoilCount = 0;
        float m_recoilLengthBase = 0.6f;
        float m_recoilLengthPerRebound = 0.5f;
        int m_dustCountBase = 4;
        float m_dustCountMassLimit = 4.0f;
        float m_dustScaleBase = 0.8f;
        float m_dustScalePerRootMass = 0.3f;
        float m_dustScalePerPower = 0.5f;
        float m_landDustRadiusBase = 1.2f;
        float m_landDustRadiusPerFallSpeed = 0.04f;
        int m_coreHoldMax = 10;
        int m_wideCoreHoldLast = 1;
        float m_missSparkSideShare = 0.7f;
        int m_centerSparkStart = 3;
        float m_scrapeLaunchWeight = 1.0f;
        float m_scrapeLiftWeight = 1.0f;
        int m_glowDimStart = 3;
        float m_glowRiseShare = 0.9f;
        int m_ringStart = 3;
        int m_ringFull = 9;
        int m_ringEnd = 10;
        int m_streakTailFrames = 3;
        int m_streakFadeFrames = 3;
        int m_recoilDelay = 4;
        //! 弱い当たりから強い当たりへ火花の数を振る威力の範囲
        float m_powerMin = 0.7f;
        float m_powerMax = 2.0f;
        //! 輪を視線に起こし、止めの間も光を脈打たせる
        float m_ringLaunchWeight = 0.6f;
        float m_coreHoldPulse = 0.85f;
        float m_wideFlashFloor = 0.74f;
        float m_wideFlashDecay = 0.4f;
        //! 粉が自機を覆わないよう向こうへずらす割合
        float m_dustAwayShare = 0.7f;
        int m_reboundTrailFadeSteps = 8;
        int m_landDustLife = 30;
        //! 粉が床へ埋まらないよう浮かせる高さ。単位はメートル
        float m_dustRingLift = 0.3f;
        //! 筋が玉の陰に入らない傾き。玉の半径と筋の半幅の和を、筋の中心までの長さで割る
        float m_reboundStreakMinAcross = 0.8f / 1.3f;
        NS::EffectAsset* m_coreAsset = nullptr;
        NS::EffectAsset* m_streakAsset = nullptr;
        NS::EffectAsset* m_ringAsset = nullptr;
        NS::EffectAsset* m_sparksAsset = nullptr;
        NS::EffectAsset* m_embersAsset = nullptr;
        NS::EffectAsset* m_glowAsset = nullptr;
        NS::EffectAsset* m_recoilAsset = nullptr;
        NS::EffectAsset* m_dustAsset = nullptr;
        NS::EffectAsset* m_reboundTrailAsset = nullptr;
        NS::EffectAsset* m_landDustAsset = nullptr;
        //! 同じ面で描画がちらつかないための離隔
        float m_floorLift = 0.03f;
        float m_landDustAssetRadius = 1.2f;

        NS_REFLECT_BEGIN(ImpactEffectSettings, NS::Object)
        NS_GROUP("核")
        NS_FIELD(m_coreDiameterBase, "核の直径の基準")
        NS_FIELD(m_coreDiameterPerPower, "核の直径の威力あたり")
        NS_FIELD(m_coreDiameterMax, "核の直径の上限")
        NS_FIELD(m_coreBirthScale, "核の出始めの大きさ")
        NS_GROUP("光条")
        NS_FIELD(m_streakLengthBase, "光条の長さの基準")
        NS_FIELD(m_streakLengthPerPower, "光条の長さの威力あたり")
        NS_FIELD(m_streakEndThickness, "光条の細りきった太さ")
        NS_GROUP("輪")
        NS_FIELD(m_ringRadiusBase, "輪の半径の基準")
        NS_FIELD(m_ringRadiusPerPower, "輪の半径の威力あたり")
        NS_FIELD(m_ringStartRadius, "輪の出始めの半径")
        NS_FIELD(m_ringFaceCamera, "輪をカメラへ起こす割合")
        NS_GROUP("火花と火の粉")
        NS_FIELD(m_sparkCountMin, "火花の数の下限")
        NS_FIELD(m_sparkCountMax, "火花の数の上限")
        NS_FIELD(m_sparkSpeedBase, "火花の速さの基準")
        NS_FIELD(m_sparkSpeedPerLaunch, "火花の速さの飛ばしの比あたり")
        NS_FIELD(m_wideSparkCount, "大きな外れの火花の数")
        NS_FIELD(m_wideSparkSpeed, "大きな外れの火花の速さ")
        NS_FIELD(m_wideSparkScale, "大きな外れの火花の大きさ")
        NS_FIELD(m_emberShare, "火の粉の数の火花あたり")
        NS_GROUP("照り")
        NS_FIELD(m_glowDiameterBase, "照りの直径の基準")
        NS_FIELD(m_glowDiameterPerPower, "照りの直径の威力あたり")
        NS_GROUP("弾かれ線")
        NS_FIELD(m_recoilCount, "弾かれ線の本数")
        NS_FIELD(m_wideRecoilCount, "大きな外れの弾かれ線の本数")
        NS_FIELD(m_recoilLengthBase, "弾かれ線の長さの基準")
        NS_FIELD(m_recoilLengthPerRebound, "弾かれ線の長さの反動の比あたり")
        NS_GROUP("当たりの粉")
        NS_FIELD(m_dustCountBase, "当たりの粉の数の基準")
        NS_FIELD(m_dustCountMassLimit, "当たりの粉の数を増やす質量の上限")
        NS_FIELD(m_dustScaleBase, "当たりの粉の大きさの基準")
        NS_FIELD(m_dustScalePerRootMass, "当たりの粉の大きさの質量の平方根あたり")
        NS_FIELD(m_dustScalePerPower, "当たりの粉の大きさの威力あたりの伸び")
        NS_GROUP("着地の粉")
        NS_FIELD(m_landDustRadiusBase, "着地の粉の半径の基準")
        NS_FIELD(m_landDustRadiusPerFallSpeed, "着地の粉の半径の落ちる速さあたり")
        NS_GROUP("時間と向き")
        NS_FIELD(m_coreHoldMax, "核の留まりの上限フレーム")
        NS_FIELD(m_wideCoreHoldLast, "外れの核の留まりフレーム")
        NS_FIELD(m_missSparkSideShare, "外れの火花の横の割合")
        NS_FIELD(m_centerSparkStart, "中心の火花の開始フレーム")
        NS_FIELD(m_scrapeLaunchWeight, "擦る火花の飛ぶ向きの重み")
        NS_FIELD(m_scrapeLiftWeight, "擦る火花の上向きの重み")
        NS_FIELD(m_glowDimStart, "照りが減り始めるフレーム")
        NS_FIELD(m_glowRiseShare, "照りの山の前の面積の割合")
        NS_FIELD(m_ringStart, "輪の開始フレーム")
        NS_FIELD(m_ringFull, "輪が広がり切るフレーム")
        NS_FIELD(m_ringEnd, "輪の終了フレーム")
        NS_FIELD(m_streakTailFrames, "光条の尾のフレーム")
        NS_FIELD(m_streakFadeFrames, "光条の薄れるフレーム")
        NS_FIELD(m_recoilDelay, "弾かれ線の遅れフレーム")
        NS_FIELD(m_powerMin, "火花の数を振る威力の下限")
        NS_FIELD(m_powerMax, "火花の数を振る威力の上限")
        NS_FIELD(m_ringLaunchWeight, "輪の飛ぶ向きの重み")
        NS_FIELD(m_coreHoldPulse, "核の脈の倍率")
        NS_FIELD(m_wideFlashFloor, "外れの光の面積の落ち先")
        NS_FIELD(m_wideFlashDecay, "外れの光の面積の減衰")
        NS_FIELD(m_dustAwayShare, "当たりの粉を向こうへずらす割合")
        NS_FIELD(m_reboundTrailFadeSteps, "反動の尾の薄れるフレーム")
        NS_FIELD(m_landDustLife, "着地の粉の寿命フレーム")
        NS_FIELD(m_dustRingLift, "着地の粉を浮かせる高さ")
        NS_FIELD(m_reboundStreakMinAcross, "反動の尾の最小横成分")
        NS_FIELD(m_coreAsset, "核の資産")
        NS_FIELD(m_streakAsset, "光条の資産")
        NS_FIELD(m_ringAsset, "輪の資産")
        NS_FIELD(m_sparksAsset, "火花の資産")
        NS_FIELD(m_embersAsset, "火の粉の資産")
        NS_FIELD(m_glowAsset, "照りの資産")
        NS_FIELD(m_recoilAsset, "弾かれ線の資産")
        NS_FIELD(m_dustAsset, "当たりの粉の資産")
        NS_FIELD(m_reboundTrailAsset, "反動の尾の資産")
        NS_FIELD(m_landDustAsset, "着地の粉の資産")
        NS_FIELD(m_floorLift, "照りを浮かせる高さ")
        NS_FIELD(m_landDustAssetRadius, "着地の粉の資産半径")
        NS_REFLECT_END()
    };
} // namespace GL::Player
