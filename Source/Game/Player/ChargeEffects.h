#pragma once

#include "Game/Player/EffectLayerList.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Object/Component.h"
#include "NSlib/Object/Reflection/Reflection.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class Player;

namespace NS::Game::Level
{
    class ImpactResolver;
} // namespace NS::Game::Level

namespace NS::Game::Player
{
    class PlayerAppearance;

    //! 溜めと放しの演出を出し、層の寿命を管理する
    //! 時間と向きは自分の保存欄から読む
    //! PlayerAppearance の後に見た目の段で更新する
    //! 描画のない世界でも層の記録を残す
    class ChargeEffects : public NS::Obj::Component
    {
    public:
        ChargeEffects() noexcept;

        //! 同居する部品を控え、描画のある世界なら受け持つ層の絵を読み込む
        void OnStart() override;
        //! 記録のフレームを 1 つ進め、このフレームの結末を読んで層を出す・付いていかせる・消す
        void OnUpdate() override;
        //! 残っている層を全部消す
        void OnEndPlay() override;

        //! 出すと決めた層の記録
        [[nodiscard]] const EffectLayerList& Layers() const noexcept { return m_layers; }

        //! @brief 溜まる光へ動的入力 1 番で渡す、溜めきってからの数
        //! @return 押し続けている間、溜めきったフレームを 1 とした数。溜めきる前と押していない間は 0
        [[nodiscard]] int FramesSinceFullCharge() const noexcept { return m_framesSinceFull; }

        //! @brief 放しの弾けの大きさの倍率
        //! @param[in] charge01 突進を出した時の溜め量 0..1。範囲の外は丸める。有限でなければ 0 とみなす
        //! @return 欄「通常突進の弾けの大きさ」+ 欄「溜めきりで足す弾けの大きさ」× 溜め量。
        //! 既定で通常突進 0.75、溜めきり 1
        [[nodiscard]] float ReleaseBurstScale(float charge01) const noexcept;

        //! @brief +Z を水平の向き direction へ向ける Y 軸まわりの回転
        //! @param[in] direction 向ける先。縦の成分は使わない
        //! @return 回転。水平の長さが 0 か有限でなければ回さない
        [[nodiscard]] static NS::Quaternion YawToward(const NS::Vector3& direction) noexcept;

        NS_REFLECT_BEGIN(ChargeEffects, NS::Obj::Component)
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
        void ForgetEndedLayers() noexcept;
        void StartPress(NS::Gfx::EffectScene* effects, const NS::Vector3& center);
        void StartCharging(NS::Gfx::EffectScene* effects, const NS::Vector3& center);
        void StartFullFlash(NS::Gfx::EffectScene* effects, const NS::Vector3& center);
        // 紫の揺れが端へ来たフレームに、その側へ火花を散らす
        void PlaySwaySparks(NS::Gfx::EffectScene* effects, const NS::Vector3& center);
        // 押している間の層を玉へ付いていかせ、溜め量を渡す
        void FollowHeldLayers(NS::Gfx::EffectScene* effects, const NS::Vector3& center, float charge01) noexcept;
        void ClearHeldLayers(NS::Gfx::EffectScene* effects) noexcept;
        void StartRelease(NS::Gfx::EffectScene* effects, const NS::Vector3& center);
        void UpdateTrail(NS::Gfx::EffectScene* effects, const NS::Vector3& center, bool slamming) noexcept;
        void StopTrailRoot(NS::Gfx::EffectScene* effects) noexcept;
        // 層を今の位置・向き・大きさへ置き直し、向きを記録に書く。描画の無い世界では記録だけ書く
        void Place(NS::Gfx::EffectScene* effects,
                   std::uint32_t id,
                   const NS::Vector3& position,
                   const NS::Quaternion& rotation) noexcept;
        void SetCharge(NS::Gfx::EffectScene* effects, std::uint32_t id, float charge01) const noexcept;
        // 動的入力 index 番に value を入れる。描画の無い世界か、再生できなかった層なら何もしない
        void SetInput(NS::Gfx::EffectScene* effects, std::uint32_t id, int index, float value) const noexcept;
        // 押している間に狙っている水平の向き。狙いの線があればその向き、無ければ Player::AimDirection
        [[nodiscard]] NS::Vector3 HeldAimDirection() const noexcept;
        void StopLayer(NS::Gfx::EffectScene* effects, std::uint32_t& id) noexcept;
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

        // 放しの弾けの輪・丸屋根・筋の大きさ。溜めきりで輪が半径 3.2 m まで広がる
        // 通常突進の 0.75 は輪が半径 2.4 m で、押した瞬間の丸まりの殻 (半径 1.2 m) の倍。0.4 (半径 1.3 m) は
        // 放した次のフレームの輪が殻と同じ大きさに見えた
        float m_tapBurstScale = 0.75f;
        float m_fullBurstScaleGain = 0.25f;
        // 揺れが端へ来るたびにその側へ散らす火花の数と速さ (m/秒)。紫の深さで始めから終わりへ上げ、勝手に出る直前ほど
        // バチバチを強くする。当たりの大きな外れの火花 (16 本・4 m/秒) より少なく遅い所から始め、終わりで並ぶ
        int m_overchargeSparkCountMin = 4;
        int m_overchargeSparkCountMax = 14;
        float m_overchargeSparkSpeedMin = 2.5f;
        float m_overchargeSparkSpeedMax = 5.0f;

        EffectLayerList m_layers;

        // 出ている層の記録の番号。0 は出ていない
        std::uint32_t m_curl = 0;
        std::uint32_t m_spin = 0;
        std::uint32_t m_grind = 0;
        std::uint32_t m_gather = 0;
        std::uint32_t m_full = 0;
        std::uint32_t m_trail = 0;
        std::uint32_t m_burst = 0;

        int m_fullStep = 0;               // 溜めきりの閃きを出したフレームの記録の番号
        int m_framesSinceFull = 0;        // FramesSinceFullCharge の値。OnUpdate が毎フレーム決める
        bool m_fullShown = false;         // この押しで溜めきりの閃きを出したか。押し直すまで 2 回目を出さない
        bool m_wasHeld = false;           // 前のフレームに押していたか
        bool m_wasSlamming = false;       // 前のフレームに突進していたか
        bool m_trailAwaitsFreeze = false; // 当たりを検知して突進が終わった。次のフレームの止めの頭で尾の親を止める
        float m_spinDegrees = 0.0f;       // 押してから玉が回った角度の累計。回転の弧の板の回りの角度
        NS::Vector3 m_slamDirection;      // 突進の尾を向ける水平の向き

        const ::Player* m_actor = nullptr;              // 押し・溜め量・突進の速度・狙いの向きを答える自機。非所有
        const PlayerAppearance* m_appearance = nullptr; // 玉の回転の正。非所有
        const NS::Game::Level::ImpactResolver* m_resolver = nullptr; // 止めの頭の正。非所有
    };
} // namespace NS::Game::Player
