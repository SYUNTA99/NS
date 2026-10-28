#pragma once

#include "NSlib/CoreObject/Asset/EffectAsset.h"
#include "NSlib/CoreObject/Object.h"

namespace GL::Player
{
    class ChargeEffectSettings : public NS::Object
    {
    public:
        // 放しの弾けのうち、輪・丸屋根・筋に掛ける大きさ
        float m_tapBurstScale = 0.75f;
        float m_fullBurstScaleGain = 0.25f;
        // 溜めすぎの深さに応じて火花の数と速さ (m/s) を上げる
        int m_overchargeSparkCountMin = 4;
        int m_overchargeSparkCountMax = 14;
        float m_overchargeSparkSpeedMin = 2.5f;
        float m_overchargeSparkSpeedMax = 5.0f;
        //! 演出の寿命と接触前に消す猶予。単位は固定フレーム
        int m_curlSteps = 6;
        int m_fullFlashSteps = 6;
        int m_burstSteps = 21;
        int m_burstClearFrames = 3;
        int m_trailFadeSteps = 6;
        //! 紫の火花の横方向に混ぜる上向きの重み
        float m_sideLift = 1.0f;
        NS::EffectAsset* m_curlAsset = nullptr;
        NS::EffectAsset* m_spinAsset = nullptr;
        NS::EffectAsset* m_grindAsset = nullptr;
        NS::EffectAsset* m_gatherAsset = nullptr;
        NS::EffectAsset* m_fullAsset = nullptr;
        NS::EffectAsset* m_burstAsset = nullptr;
        NS::EffectAsset* m_trailAsset = nullptr;
        NS::EffectAsset* m_swaySparksAsset = nullptr;

        NS_REFLECT_BEGIN(ChargeEffectSettings, NS::Object)
        NS_GROUP("放しの弾け")
        NS_FIELD(m_tapBurstScale, "通常突進の弾けの大きさ")
        NS_FIELD(m_fullBurstScaleGain, "溜めきりで足す弾けの大きさ")
        NS_GROUP("紫の火花")
        NS_FIELD(m_overchargeSparkCountMin, "紫の火花の数の始め")
        NS_FIELD(m_overchargeSparkCountMax, "紫の火花の数の終わり")
        NS_FIELD(m_overchargeSparkSpeedMin, "紫の火花の速さの始め")
        NS_FIELD(m_overchargeSparkSpeedMax, "紫の火花の速さの終わり")
        NS_GROUP("時間と向き")
        NS_FIELD(m_curlSteps, "丸まりの殻の寿命フレーム")
        NS_FIELD(m_fullFlashSteps, "溜めきりの閃きの寿命フレーム")
        NS_FIELD(m_burstSteps, "放しの弾けの寿命フレーム")
        NS_FIELD(m_burstClearFrames, "放しの弾けを消す接触前フレーム")
        NS_FIELD(m_trailFadeSteps, "突進の尾の薄れるフレーム")
        NS_FIELD(m_sideLift, "紫の火花の上向きの重み")
        NS_FIELD(m_curlAsset, "丸まりの殻の資産")
        NS_FIELD(m_spinAsset, "回転の弧の資産")
        NS_FIELD(m_grindAsset, "溜めの擦れの資産")
        NS_FIELD(m_gatherAsset, "溜まる光の資産")
        NS_FIELD(m_fullAsset, "溜めきりの資産")
        NS_FIELD(m_burstAsset, "放しの弾けの資産")
        NS_FIELD(m_trailAsset, "突進の尾の資産")
        NS_FIELD(m_swaySparksAsset, "紫の火花の資産")
        NS_REFLECT_END()
    };
} // namespace GL::Player
