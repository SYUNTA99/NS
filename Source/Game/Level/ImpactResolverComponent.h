#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/OverlayRendererComponent.h"

#include <cstdint>

namespace NS::Object
{
    class GameObject;
} // namespace NS::Object

namespace NS::Game::Player
{
    class PlayerComponent;
}

namespace NS::Game::Level
{
    class BreakableComponent;
    class CollisionInputComponent;
    class LaunchedBodyComponent;
    class MomentumComponent;

    //! @brief ぶつかった結果を自機側で決める Component
    //! @details 帯は Update より前。PlayerComponent が動く前にその 1 固定ステップの結末を決めるので、
    //! 壁の手前で止められて速度を消された後から結果を推測し直さずに済む
    //! 相手は World::ForEachComponent で BreakableComponent を回って自分で探す
    //! NS::Physics は NS::Object を知らない決まりなので、掃引の戻り値から相手を引く経路は使えない
    //! 衝突の瞬間は自機を数固定ステップ止め、反発・発射・破壊・猶予の開始を明けた歩へ保留する
    //! 依存: NS::Game::Player::PlayerComponent, MomentumComponent, BreakableComponent, LaunchedBodyComponent,
    //! CollisionInputComponent
    class ImpactResolverComponent : public NS::Object::OverlayRendererComponent
    {
    public:
        ImpactResolverComponent() noexcept;

        //! 同じ配置物の移動と勢いを引き当てる。どちらか無ければ以後何もしない
        void OnStart() override;

        //! この固定ステップで重なる壊せる物を探し、向かっていれば止めてから破壊するか、反発と押し飛ばしを与える
        void OnUpdate() override;

        //! 直近の更新で反発を検知した場合 true、それ以外の場合は false
        [[nodiscard]] bool DidRebound() const noexcept { return m_didRebound; }

        //! 直近の更新で貫通を検知した場合 true、それ以外の場合は false
        [[nodiscard]] bool DidBreak() const noexcept { return m_didBreak; }

        //! 直近の裁定がピークで当たった場合 true、それ以外の場合は false
        [[nodiscard]] bool WasPeakImpact() const noexcept { return m_wasPeakImpact; }

        //! 直近の裁定で読んだ溜め量 0..1
        [[nodiscard]] float LastCharge01() const noexcept { return m_lastCharge01; }

        //! 直近の裁定の突進位置係数
        [[nodiscard]] float LastPositionFactor() const noexcept { return m_lastPositionFactor; }

        //! 直近の裁定の最終威力
        [[nodiscard]] float LastPower() const noexcept { return m_lastPower; }

        //! 凍結の途中で外れても移動を止めたままにしない
        void OnEndPlay() override;

        //! ピークで当てた直後だけ、歩ごとに減衰する白を画面全体へ重ねる
        void OnRenderOverlay(const NS::Graphics::RenderContext& ctx) override;

        //! 潰した形で凍結中か、伸びから元の形へ戻している途中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsScaleAnimating() const noexcept { return m_scaleHeld || m_recoverRemaining > 0; }

        // 返り方は当てた時の手触りそのもの。プレイ中に Inspector で触って詰められるよう公開する
        NS_REFLECT_BEGIN(ImpactResolverComponent, NS::Object::OverlayRendererComponent)
        NS_REFLECT_FIELD(m_reboundSpeed, "反発基準初速")
        NS_REFLECT_FIELD(m_reboundUpSpeed, "反発の上向き初速")
        NS_REFLECT_FIELD(m_launchSpeed, "押し飛ばし基準初速")
        NS_REFLECT_FIELD(m_launchUpScale, "押し飛ばしの浮き上がり")
        NS_REFLECT_FIELD(m_hitStopBaseSeconds, "ヒットストップ基準秒")
        NS_REFLECT_FIELD(m_peakHitStopScale, "ピークのヒットストップ倍率")
        NS_REFLECT_FIELD(m_pushInDistance, "食い込み距離")
        NS_REFLECT_FIELD(m_shakeAmplitude, "振動の振幅")
        NS_REFLECT_FIELD(m_cameraShakeScale, "カメラ揺れの強さ")
        NS_REFLECT_FIELD(m_squashThickness, "潰れの厚み")
        NS_REFLECT_FIELD(m_squashHeight, "潰れの伸び上がり")
        NS_REFLECT_FIELD(m_stretchAlong, "弾け伸びの倍率")
        NS_REFLECT_FIELD(m_breakEnabled, "破壊を許可")
        NS_REFLECT_FIELD(m_breakSpeedScale, "貫通時の減速倍率")
        NS_REFLECT_FIELD(m_breakStopSeconds, "貫通の止め秒")
        NS_REFLECT_FIELD(m_debrisCount, "破片の数")
        NS_REFLECT_FIELD(m_debrisSpeed, "破片の初速")
        NS_REFLECT_FIELD(m_debrisLifeSeconds, "破片の残る秒")
        NS_REFLECT_FIELD(m_powerFloorRatio, "威力の下限比")
        NS_REFLECT_END()

    private:
        // 重なっている壊せる物のうち中心が最も近い 1 体。無ければ nullptr
        // 事前条件: m_movement が非 null
        [[nodiscard]] BreakableComponent* FindOverlapped() const;

        // 凍結を掛ける。自機を寝かせて潰し、相手を食い込ませ、カメラを揺らし始める
        void BeginFreeze(int stopSteps);

        // 解放後の歩で伸びた形から配置で決めた元の形へ滑らかに戻す。最後の歩は控えた値を厳密に書く
        void RecoverScale();

        // 進行の軸だけ倍率を効かせた描画スケールを作る。縦は別の倍率で受ける
        [[nodiscard]] NS::Core::Vector3 ScaledAlongImpact(float along, float height) const noexcept;

        // 止めていた結果を適用する。自機を起こして速度を書き、反発なら猶予と発射、貫通なら破壊を行う
        void ReleaseHitStop();

        // 壊れた物の印と当たりを寝かせ、見た目を差し替える。配置物は消さない
        void BreakTarget(NS::Object::GameObject& target);

        // 秒を固定ステップの歩数へ換算して 0〜12 に丸める
        [[nodiscard]] int SecondsToSteps(float seconds) const noexcept;

        // 凍結中の歩で、相手を発射軸に沿って食い込み位置の周りで往復させる。絵だけで当たりは動かさない
        void ApplyFreezeVibration();

        // 最終威力と質量から止める歩数を出す。0 なら止めない
        [[nodiscard]] int ComputeHitStopSteps(float power, float mass, float hitStopScale) const noexcept;

        // 壊れた位置へ破片を撒く。向きは番号から決めるので同じ状況では同じ散り方になる
        void SpawnDebris(const NS::Core::Vector3& origin, float mass);

        float m_reboundSpeed = 9.0f;   // 動かない壁に通常速度で当たった時の返りの速さ
        float m_reboundUpSpeed = 3.0f; // 反発の上向き初速
        float m_launchSpeed = 20.0f;   // 通常速度で質量 1 の物に与える水平初速
        float m_launchUpScale = 0.35f; // 水平初速に対する上向きの比
        // 既定の固定ステップ (1/60 秒) の 4 歩ぶん
        float m_hitStopBaseSeconds = 4.0f / 60.0f; // 質量 1 へ通常速度で当てた時に止める秒
        float m_peakHitStopScale = 2.0f;           // 威力の伸び (最大 2 倍) と掛けて、素とピークの止まりを 4 倍差にする
        float m_pushInDistance = 0.06f;            // 凍結の頭で相手を発射方向へ食い込ませる距離
        float m_shakeAmplitude = 0.05f;            // 凍結中の往復の振れ幅。質量 1 で半分になる
        float m_cameraShakeScale = 0.06f;          // カメラ揺れの上下振れ幅の基準
        float m_squashThickness = 0.7f;            // 凍結中の進行方向の厚みの倍率
        float m_squashHeight = 1.1f;               // 凍結中の高さの倍率
        float m_stretchAlong = 1.2f;               // 解放の歩の弾かれる方向の倍率
        // 既定は壊さない。壊れて消えると重さが飛距離に出ず、押し飛ばしと反発だけを先に詰められない
        bool m_breakEnabled = false;
        float m_breakSpeedScale = 0.75f;         // 貫通した直後に速度へ掛ける倍率
        float m_breakStopSeconds = 4.0f / 60.0f; // 貫通の瞬間に止める秒。4 歩ぶん
        int m_debrisCount = 5;                   // 貫通した時に出す破片の数
        float m_debrisSpeed = 6.0f;              // 質量 1 の物を壊した時の破片の水平初速
        float m_debrisLifeSeconds = 8.0f; // 破片が止まってから消えるまでの秒。押し飛ばした配置物と違い破片は残さない
        // 0.5 は歩き 4 ÷ 通常 8。立ち止まりの発動にも歩きで当てた程度の勢いを残す
        float m_powerFloorRatio = 0.5f;

        int m_freezePendingSteps = 0;                                // 次の歩に掛ける凍結の歩数。0 は予約なし
        int m_hitStopRemaining = 0;                                  // 止まっている残り歩数。0 は止まっていない
        int m_hitStopTotal = 0;                                      // 止め始めの歩数。振動の減衰の分母
        NS::Core::Vector3 m_pendingSelfVelocity{0.0f, 0.0f, 0.0f};   // 明けた歩に自機へ書く反発速度
        NS::Core::Vector3 m_pendingLaunchVelocity{0.0f, 0.0f, 0.0f}; // 明けた歩に相手へ渡す発射速度
        NS::Core::Vector3 m_pendingTargetHome{0.0f, 0.0f, 0.0f};     // 相手の元位置。明けた歩に厳密に戻す
        NS::Core::Vector3 m_pendingImpactDir{0.0f, 0.0f, 0.0f};      // 発射の水平方向。食い込みと振動の軸
        float m_pendingShakeAmplitude = 0.0f;                        // この衝突の往復の振れ幅
        float m_pendingShakeStrength = 0.0f;                         // この衝突のカメラ揺れの振れ幅
        NS::Core::Vector3 m_scaleHome{1.0f, 1.0f, 1.0f};             // 配置で決めた元の描画スケールの控え
        NS::Core::Vector3 m_stretchScale{1.0f, 1.0f, 1.0f};          // 解放の歩の伸びた形
        int m_recoverRemaining = 0;                                  // 形を戻し切るまでの残り歩数
        bool m_scaleHeld = false;                                    // 潰した形のまま凍結している最中か
        std::uint32_t m_pendingTargetId = 0;                         // 発射する相手の永続 id

        bool m_didRebound = false;   // 直近の更新で反発を検知したか
        bool m_didBreak = false;     // 直近の更新で貫通を検知したか
        bool m_pendingBreak = false; // 保留中の結果が貫通か
        bool m_wasPeakImpact = false;
        float m_lastCharge01 = 0.0f;
        float m_lastPositionFactor = 0.0f;
        float m_lastPower = 0.0f;
        int m_peakFlashRemaining = 0;
        NS::Game::Player::PlayerComponent* m_movement = nullptr; // 同じ配置物の移動。非所有
        MomentumComponent* m_momentum = nullptr;                 // 同じ配置物の勢い。非所有
        CollisionInputComponent* m_collisionInput = nullptr;
    };
} // namespace NS::Game::Level
