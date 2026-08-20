#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"

#include <cstdint>

namespace NS::Object
{
    class CharacterMovementComponent;
}

namespace NS::Game::Level
{
    class BreakableComponent;
    class LaunchedBodyComponent;
    class MomentumComponent;

    //! @brief ぶつかった結果を自機側で決める Component
    //! @details 帯は Update より前。CharacterMovementComponent が動く前にその 1 固定ステップの結末を決めるので、
    //! 壁の手前で止められて速度を消された後から結果を推測し直さずに済む
    //! 相手は World::ForEachComponent で BreakableComponent を回って自分で探す
    //! NS::Physics は NS::Object を知らない決まりなので、掃引の戻り値から相手を引く経路は使えない
    //! 衝突の瞬間は自機を数固定ステップ止め、反発・発射・猶予の開始を明けた歩へ保留する
    //! 依存: NS::Object::CharacterMovementComponent, MomentumComponent, BreakableComponent, LaunchedBodyComponent
    class ImpactResolverComponent : public NS::Object::Component
    {
    public:
        ImpactResolverComponent() noexcept;

        //! 同じ配置物の移動と勢いを引き当てる。どちらか無ければ以後何もしない
        void OnStart() override;

        //! この固定ステップで重なる壊せる物を探し、向かっていれば止めてから反発と押し飛ばしを与える
        void OnUpdate() override;

        //! 直近の更新で衝突を検知した場合 true、それ以外の場合は false
        [[nodiscard]] bool DidRebound() const noexcept { return m_didRebound; }

        // 返り方は当てた時の手触りそのもの。プレイ中に Inspector で触って詰められるよう公開する
        NS_REFLECT_BEGIN(ImpactResolverComponent, NS::Object::Component)
        NS_REFLECT_FIELD(m_reboundSpeed, "反発基準初速")
        NS_REFLECT_FIELD(m_reboundUpSpeed, "反発の上向き初速")
        NS_REFLECT_FIELD(m_launchSpeed, "押し飛ばし基準初速")
        NS_REFLECT_FIELD(m_launchUpScale, "押し飛ばしの浮き上がり")
        NS_REFLECT_FIELD(m_hitStopBaseSeconds, "ヒットストップ基準秒")
        NS_REFLECT_FIELD(m_pushInDistance, "食い込み距離")
        NS_REFLECT_FIELD(m_shakeAmplitude, "振動の振幅")
        NS_REFLECT_FIELD(m_cameraShakeScale, "カメラ揺れの強さ")
        NS_REFLECT_END()

    private:
        // 重なっている壊せる物のうち中心が最も近い 1 体。無ければ nullptr
        // 事前条件: m_movement が非 null
        [[nodiscard]] BreakableComponent* FindOverlapped() const;

        // 止めていた結果を適用する。自機を起こして反発速度を書き、猶予を始め、相手を発射する
        void ReleaseHitStop();

        // 凍結中の歩で、相手を発射軸に沿って食い込み位置の周りで往復させる。絵だけで当たりは動かさない
        void ApplyFreezeVibration();

        // 勢いの比と質量から止める歩数を出す。0 なら止めない
        [[nodiscard]] int ComputeHitStopSteps(float ratio, float mass) const noexcept;

        float m_reboundSpeed = 9.0f;   // 動かない壁に通常速度で当たった時の返りの速さ
        float m_reboundUpSpeed = 3.0f; // 反発の上向き初速
        float m_launchSpeed = 14.0f;   // 通常速度で質量 1 の物に与える水平初速
        float m_launchUpScale = 0.35f; // 水平初速に対する上向きの比
        // 既定の固定ステップ (1/60 秒) の 4 歩ぶん
        float m_hitStopBaseSeconds = 4.0f / 60.0f; // 質量 1 へ通常速度で当てた時に止める秒
        float m_pushInDistance = 0.06f;            // 凍結の頭で相手を発射方向へ食い込ませる距離
        float m_shakeAmplitude = 0.05f;            // 凍結中の往復の振れ幅。質量 1 で半分になる
        float m_cameraShakeScale = 0.06f;          // カメラ揺れの上下振れ幅の基準

        int m_hitStopRemaining = 0;                                  // 止まっている残り歩数。0 は止まっていない
        int m_hitStopTotal = 0;                                      // 止め始めの歩数。振動の減衰の分母
        NS::Core::Vector3 m_pendingSelfVelocity{0.0f, 0.0f, 0.0f};   // 明けた歩に自機へ書く反発速度
        NS::Core::Vector3 m_pendingLaunchVelocity{0.0f, 0.0f, 0.0f}; // 明けた歩に相手へ渡す発射速度
        NS::Core::Vector3 m_pendingTargetHome{0.0f, 0.0f, 0.0f};     // 相手の元位置。明けた歩に厳密に戻す
        NS::Core::Vector3 m_pendingImpactDir{0.0f, 0.0f, 0.0f};      // 発射の水平方向。食い込みと振動の軸
        float m_pendingShakeAmplitude = 0.0f;                        // この衝突の往復の振れ幅
        std::uint32_t m_pendingTargetId = 0;                         // 発射する相手の永続 id

        bool m_didRebound = false;                                    // 直近の更新で衝突を検知したか
        NS::Object::CharacterMovementComponent* m_movement = nullptr; // 同じ配置物の移動。非所有
        MomentumComponent* m_momentum = nullptr;                      // 同じ配置物の勢い。非所有
    };
} // namespace NS::Game::Level
