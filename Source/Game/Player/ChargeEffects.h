#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/EffectTypes.h"
#include "NSlib/Object/Reflection/Reflection.h"
#include "NSlib/Object/SubObject.h"

#include "NSlib/Core/Types.h"
#include <string>
#include <string_view>
#include <vector>

class PlayerActor;

namespace GL::Level
{
    class ImpactResolver;
} // namespace GL::Level

namespace GL::Player
{
    class PlayerAppearance;

    //! 溜めと放しの演出を出し、寿命を管理する部品
    //! PlayerAppearance の後に更新する
    class ChargeEffects : public NS::Obj::SubObject
    {
    public:
        ChargeEffects() noexcept;

        void OnStart() override;
        void OnUpdate() override;
        //! 番号を控えている溜めと放しのエフェクトを消す
        void OnEndPlay() override;

        //! @brief 溜まる光へ動的入力 1 番で渡す、溜めきってからの数
        //! @return 押し続けている間、溜めきったフレームを 1 とした数。溜めきる前と押していない間は 0
        [[nodiscard]] int FramesSinceFullCharge() const noexcept { return m_framesSinceFull; }

        //! 放しの弾けの倍率を返す。charge01 は 0〜1 に丸め、非有限は 0 とする
        [[nodiscard]] float ReleaseBurstScale(float charge01) const noexcept;

        //! +Z を direction の水平成分へ向ける。水平の向きが無効なら無回転
        [[nodiscard]] static NS::Quaternion YawToward(const NS::Vector3& direction) noexcept;

        NS_REFLECT_BEGIN(ChargeEffects, NS::Obj::SubObject)
        NS_REFLECT_GROUP("放しの弾け")
        NS_REFLECT_FIELD(m_tapBurstScale, "通常突進の弾けの大きさ")
        NS_REFLECT_FIELD(m_fullBurstScaleGain, "溜めきりで足す弾けの大きさ")
        NS_REFLECT_GROUP("紫の火花")
        NS_REFLECT_FIELD(m_overchargeSparkCountMin, "紫の火花の数の始め")
        NS_REFLECT_FIELD(m_overchargeSparkCountMax, "紫の火花の数の終わり")
        NS_REFLECT_FIELD(m_overchargeSparkSpeedMin, "紫の火花の速さの始め")
        NS_REFLECT_FIELD(m_overchargeSparkSpeedMax, "紫の火花の速さの終わり")
        NS_REFLECT_GROUP("時間と向き")
        NS_REFLECT_FIELD(m_curlSteps, "丸まりの殻の寿命フレーム")
        NS_REFLECT_FIELD(m_fullFlashSteps, "溜めきりの閃きの寿命フレーム")
        NS_REFLECT_FIELD(m_burstSteps, "放しの弾けの寿命フレーム")
        NS_REFLECT_FIELD(m_burstClearFrames, "放しの弾けを消す接触前フレーム")
        NS_REFLECT_FIELD(m_trailFadeSteps, "突進の尾の薄れるフレーム")
        NS_REFLECT_FIELD(m_sideLift, "紫の火花の上向きの重み")
        NS_REFLECT_FIELD(m_curlAsset, "丸まりの殻の資産")
        NS_REFLECT_FIELD(m_spinAsset, "回転の弧の資産")
        NS_REFLECT_FIELD(m_grindAsset, "溜めの擦れの資産")
        NS_REFLECT_FIELD(m_gatherAsset, "溜まる光の資産")
        NS_REFLECT_FIELD(m_fullAsset, "溜めきりの資産")
        NS_REFLECT_FIELD(m_burstAsset, "放しの弾けの資産")
        NS_REFLECT_FIELD(m_trailAsset, "突進の尾の資産")
        NS_REFLECT_FIELD(m_swaySparksAsset, "紫の火花の資産")
        NS_REFLECT_END()

    private:
        std::string m_curlAsset = "charge.curl";
        std::string m_spinAsset = "charge.spin";
        std::string m_grindAsset = "charge.grind";
        std::string m_gatherAsset = "charge.gather";
        std::string m_fullAsset = "charge.full";
        std::string m_burstAsset = "release.burst";
        std::string m_trailAsset = "slam.trail";
        std::string m_swaySparksAsset = "impact.sparks";
        int m_chargeInput = 0;
        int m_burstScaleInput = 1;
        int m_fullFramesInput = 1;
        NS::Gfx::EffectPlayDesc PlayDesc(const NS::Vector3& position,
                                         const NS::Quaternion& rotation,
                                         float charge01) const noexcept;
        void ForgetEndedEffects() noexcept;
        void StartPress(const NS::Vector3& center);
        void StartCharging(const NS::Vector3& center);
        void StartFullFlash(const NS::Vector3& center);
        // 紫の揺れが端へ来たフレームに、その側へ火花を散らす
        void PlaySwaySparks(const NS::Vector3& center);
        // 押している間の層を玉へ付いていかせ、溜め量を渡す
        void FollowHeldEffects(const NS::Vector3& center, float charge01) noexcept;
        void ClearHeldEffects() noexcept;
        void StartRelease(const NS::Vector3& center);
        void UpdateTrail(const NS::Vector3& center, bool slamming) noexcept;
        void StopTrailRoot() noexcept;
        // 層を今の位置・向き・大きさへ置き直し、向きを記録に書く。描画の無い世界では記録だけ書く
        void Place(NS::uint32 id, const NS::Vector3& position, const NS::Quaternion& rotation) noexcept;
        void SetCharge(NS::uint32 id, float charge01) const noexcept;
        void SetInput(NS::uint32 id, int index, float value) const noexcept;
        // 押している間に狙っている水平の向き。狙いの線があればその向き、無ければ PlayerActor::AimDirection
        [[nodiscard]] NS::Vector3 HeldAimDirection() const noexcept;
        void StopAndClear(NS::uint32& id) noexcept;
        // 跳ね返りか破壊が起きたか、触れる見込みが m_burstClearFrames 以内か
        [[nodiscard]] bool IsContactNear() const noexcept;

        //! 演出の寿命と接触前に消す猶予。単位は固定フレーム
        int m_curlSteps = 6;
        int m_fullFlashSteps = 6;
        int m_burstSteps = 21;
        int m_burstClearFrames = 3;
        int m_trailFadeSteps = 6;
        //! 紫の火花の横方向に混ぜる上向きの重み
        float m_sideLift = 1.0f;

        // 放しの弾けのうち、輪・丸屋根・筋に掛ける大きさ
        float m_tapBurstScale = 0.75f;
        float m_fullBurstScaleGain = 0.25f;
        // 溜めすぎの深さに応じて火花の数と速さ (m/s) を上げる
        int m_overchargeSparkCountMin = 4;
        int m_overchargeSparkCountMax = 14;
        float m_overchargeSparkSpeedMin = 2.5f;
        float m_overchargeSparkSpeedMax = 5.0f;

        // 出ているエフェクトの記録の番号。0 は出ていない
        NS::uint32 m_curl = 0;
        NS::uint32 m_spin = 0;
        NS::uint32 m_grind = 0;
        NS::uint32 m_gather = 0;
        NS::uint32 m_full = 0;
        NS::uint32 m_trail = 0;
        NS::uint32 m_burst = 0;

        int m_fullStep = 0;        // 溜めきりの閃きを出したフレームの記録の番号
        int m_framesSinceFull = 0; // FramesSinceFullCharge の値。OnUpdate が毎フレーム決める
        bool m_fullShown = false;  // この押しで溜めきりの閃きを出したか。押し直すまで 2 回目を出さない
        bool m_wasHeld = false;
        bool m_wasSlamming = false;
        bool m_trailAwaitsFreeze = false; // 当たりを検知して突進が終わった。次のフレームの止めの頭で尾の親を止める
        float m_spinDegrees = 0.0f;       // 押してから玉が回った角度の累計。回転の弧の板の回りの角度
        NS::Vector3 m_slamDirection;

        const ::PlayerActor* m_actor = nullptr;              // 押し・溜め量・突進の速度・狙いの向きを答える自機。非所有
        const PlayerAppearance* m_appearance = nullptr; // 玉の回転の正。非所有
        const GL::Level::ImpactResolver* m_resolver = nullptr; // 止めの頭の正。非所有
    };
} // namespace GL::Player
