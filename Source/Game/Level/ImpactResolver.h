#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/OverlayRenderer.h"

#include <cstdint>

namespace NS::Obj
{
    class GameObject;
} // namespace NS::Obj

namespace NS::Game::Player
{
    class PlayerComponent;
}

namespace NS::Game::Level
{
    class Breakable;
    class CollisionInput;
    class LaunchedBody;

    //! @brief 当たり 1 回の裁定の内訳
    struct ImpactRecord
    {
        std::uint32_t sequence = 0;
        std::uint32_t targetId = 0;
        float power = 0.0f;
        float charge01 = 0.0f;
        float positionFactor = 0.0f;
        float cameraShake = 0.0f;
        int hitStopSteps = 0;
        bool peak = false;
        bool broke = false;
        NS::Core::Vector3 selfVelocity;
        NS::Core::Vector3 launchVelocity;
        NS::Core::Vector3 impactDir;
        NS::Core::Vector3 targetPos;
    };

    //! @brief ぶつかった結果を自機側で決める Component
    //! @details 帯は Update より前。PlayerComponent が動く前にその 1 固定ステップの結末を決めるので、
    //! 壁の手前で止められて速度を消された後から結果を推測し直さずに済む
    //! 相手は PhysicsScene::OverlapCapsule で重なった body を集め、ObjectList::ForEachComponent で回した
    //! Breakable の body と照合して決める。NS::Phys は NS::Obj を知らないので、
    //! body から持ち主を引く関数は無い
    //! 衝突の瞬間は自機を数固定ステップ止め、反発・発射・破壊を明けたフレームへ保留する
    //! 依存: NS::Game::Player::PlayerComponent, Breakable, LaunchedBody, CollisionInput
    class ImpactResolver : public NS::Obj::OverlayRenderer
    {
    public:
        ImpactResolver() noexcept;

        //! 同じ配置物の移動と体当たりの入力を引き当てる。移動が無ければ以後何もしない
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

        //! 直近の裁定の当たり位置係数
        [[nodiscard]] float LastPositionFactor() const noexcept { return m_lastPositionFactor; }

        //! 直近の裁定の最終威力
        [[nodiscard]] float LastPower() const noexcept { return m_lastPower; }

        //! 直近の当たりで控えた内訳。まだ当たっていない間は sequence が 0
        [[nodiscard]] const ImpactRecord& LastImpact() const noexcept { return m_lastImpact; }

        //! 白フラッシュの残りフレーム数。出していない場合 0
        [[nodiscard]] int PeakFlashStepsRemaining() const noexcept { return m_peakFlashRemaining; }

        //! 凍結の途中で外れても移動を止めたままにしない
        void OnEndPlay() override;

        //! ピークで当てた直後だけ、フレームごとに減衰する白を画面全体へ重ねる
        void OnRenderOverlay(const NS::Gfx::RenderContext& ctx) override;

        //! 潰した形で凍結中か、伸びから元の形へ戻している途中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsScaleAnimating() const noexcept { return m_scaleHeld || m_recoverRemaining > 0; }

        // 返り方は当てた時の手触りそのもの。プレイ中に Inspector で触って詰められるよう公開する
        NS_REFLECT_BEGIN(ImpactResolver, NS::Obj::OverlayRenderer)
        NS_REFLECT_FIELD(m_reboundSpeed, "反発基準初速")
        NS_REFLECT_FIELD(m_reboundUpSpeed, "反発の上向き初速")
        NS_REFLECT_FIELD(m_launchSpeed, "押し飛ばし基準初速")
        NS_REFLECT_FIELD(m_launchMassExponent, "押し飛ばしの質量指数")
        NS_REFLECT_FIELD(m_launchUpScale, "押し飛ばしの浮き上がり")
        NS_REFLECT_FIELD(m_launchMaxSpeed, "押し飛ばしの最高速")
        NS_REFLECT_FIELD(m_hitStopBaseSeconds, "ヒットストップ基準秒")
        NS_REFLECT_FIELD(m_peakHitStopScale, "ピークのヒットストップ倍率")
        NS_REFLECT_FIELD(m_hitStopMaxSeconds, "ヒットストップの上限秒")
        NS_REFLECT_FIELD(m_pushInDistance, "食い込み距離")
        NS_REFLECT_FIELD(m_shakeAmplitude, "振動の振幅")
        NS_REFLECT_FIELD(m_cameraShakeScale, "カメラ揺れの強さ")
        NS_REFLECT_FIELD(m_squashThickness, "潰れの厚み")
        NS_REFLECT_FIELD(m_squashHeight, "潰れの伸び上がり")
        NS_REFLECT_FIELD(m_stretchAlong, "弾け伸びの倍率")
        NS_REFLECT_FIELD(m_stretchRecoverSteps, "弾け伸びを戻すフレーム数")
        NS_REFLECT_FIELD(m_peakFlashAlpha, "ピークの白の濃さ")
        NS_REFLECT_FIELD(m_peakFlashSteps, "ピークの白のフレーム数")
        NS_REFLECT_FIELD(m_breakEnabled, "破壊を許可")
        NS_REFLECT_FIELD(m_breakSpeedScale, "貫通時の減速倍率")
        NS_REFLECT_FIELD(m_breakStopSeconds, "貫通の止め秒")
        NS_REFLECT_END()

    private:
        // 重なっている壊せる物のうち中心が最も近い 1 体。無ければ nullptr
        // 事前条件: m_movement が非 null
        [[nodiscard]] Breakable* FindOverlapped() const;

        // 凍結を掛ける。自機を寝かせて潰し、相手を食い込ませ、カメラを揺らし始める
        void BeginFreeze(int stopSteps);

        // 解放後のフレームで伸びた形から配置で決めた元の形へ滑らかに戻す。最後のフレームは控えた値を厳密に書く
        void RecoverScale();

        // 進行の軸だけ倍率を効かせた描画スケールを作る。縦は別の倍率で受ける
        [[nodiscard]] NS::Core::Vector3 ScaledAlongImpact(float along, float height) const noexcept;

        // 止めていた結果を適用する。自機を起こして速度を書き、反発なら発射、貫通なら破壊を行う
        void ReleaseHitStop();

        // 秒をフレーム数へ換算して 0 から MaxHitStopSteps までに丸める
        [[nodiscard]] int SecondsToSteps(float seconds) const noexcept;

        // 上限秒をフレーム数へ換算する。非有限と 0 以下は 0 で、止めない
        [[nodiscard]] int MaxHitStopSteps() const noexcept;

        // 凍結中のフレームで、相手を発射軸に沿って食い込み位置の周りで往復させる。絵だけで当たりは動かさない
        void ApplyFreezeVibration();

        // 最終威力と質量から止めるフレーム数を出す。0 なら止めない
        [[nodiscard]] int ComputeHitStopSteps(float power, float mass, float hitStopScale) const noexcept;

        float m_reboundSpeed = 9.0f;        // 動かない壁に通常速度で当たった時の返りの速さ
        float m_reboundUpSpeed = 3.0f;      // 反発の上向き初速
        float m_launchSpeed = 32.0f;        // 通常速度で質量 1 の物に与える水平初速
        float m_launchMassExponent = 0.35f; // 押し飛ばしの初速を割る質量の指数。1 で反比例、0 で質量を見ない
        float m_launchUpScale = 0.35f;      // 水平初速に対する上向きの比
        // 押し飛ばしの初速の上限。軽い物ほど初速が伸び、上限が無いと画面の外へ消える
        // 120 は、60 だと質量 1 以下の物を溜め切ってピークで当てた初速が 60 に揃い、軽いほど遠くへ飛ぶ差が消えるため
        float m_launchMaxSpeed = 120.0f;
        // 既定の固定ステップ (1/60 秒) の 4 フレームぶん
        float m_hitStopBaseSeconds = 4.0f / 60.0f; // 質量 1 へ通常速度で当てた時に止める秒
        float m_peakHitStopScale = 2.0f;           // 威力の伸び (最大 2 倍) と掛けて、素とピークの止まりを 4 倍差にする
        // 止める長さの上限。0.2 秒より長い停止は衝突の重さではなく処理落ちに見える
        float m_hitStopMaxSeconds = 12.0f / 60.0f;
        float m_pushInDistance = 0.06f;   // 凍結の頭で相手を発射方向へ食い込ませる距離
        float m_shakeAmplitude = 0.05f;   // 凍結中の往復の振れ幅。質量 1 で半分になる
        float m_cameraShakeScale = 0.06f; // カメラ揺れの上下振れ幅の基準
        float m_squashThickness = 0.7f;   // 凍結中の進行方向の厚みの倍率
        float m_squashHeight = 1.1f;      // 凍結中の高さの倍率
        float m_stretchAlong = 1.2f;      // 解放のフレームの弾かれる方向の倍率
        // 伸びから元の形へ戻すフレーム数。反発の滞空 0.3 秒の前半で戻し切り、着地の前に形を確定させる
        int m_stretchRecoverSteps = 6;
        // ピークで当てた時だけの白フラッシュ。端で当てた時と見間違えない強さにする
        // 0.5 は一瞬白と分かる濃さ。1.0 だと食い込みと潰れの絵が隠れる
        float m_peakFlashAlpha = 0.5f;
        // 潰れは当たった次のフレームから始まる
        // 2 フレームなら白が重なるのは潰れの最初の 1 フレームだけで、そこも濃さは半分
        // 8 フレームでは潰れの最初の 7 フレームに重なり、形が読めなかった
        int m_peakFlashSteps = 2;
        // 既定は壊さない。壊れて消えると重さが飛距離に出ず、押し飛ばしと反発だけを先に詰められない
        bool m_breakEnabled = false;
        float m_breakSpeedScale = 0.75f;         // 貫通した直後に速度へ掛ける倍率
        float m_breakStopSeconds = 4.0f / 60.0f; // 貫通の瞬間に止める秒。4 フレームぶん

        int m_freezePendingSteps = 0;                              // 次のフレームに掛ける凍結のフレーム数。0 は予約なし
        int m_hitStopRemaining = 0;                                // 止まっている残りフレーム数。0 は止まっていない
        int m_hitStopTotal = 0;                                    // 止め始めのフレーム数。振動の減衰の分母
        NS::Core::Vector3 m_pendingSelfVelocity{0.0f, 0.0f, 0.0f}; // 明けたフレームに自機へ書く反発速度
        NS::Core::Vector3 m_pendingLaunchVelocity{0.0f, 0.0f, 0.0f}; // 明けたフレームに相手へ渡す発射速度
        NS::Core::Vector3 m_pendingTargetHome{0.0f, 0.0f, 0.0f};     // 相手の元位置。明けたフレームに厳密に戻す
        NS::Core::Vector3 m_pendingImpactDir{0.0f, 0.0f, 0.0f};      // 発射の水平方向。食い込みと振動の軸
        float m_pendingShakeAmplitude = 0.0f;                        // この衝突の往復の振れ幅
        float m_pendingShakeStrength = 0.0f;                         // この衝突のカメラ揺れの振れ幅
        NS::Core::Vector3 m_scaleHome{1.0f, 1.0f, 1.0f};             // 配置で決めた元の描画スケールの控え
        NS::Core::Vector3 m_stretchScale{1.0f, 1.0f, 1.0f};          // 解放のフレームの伸びた形
        int m_recoverRemaining = 0;                                  // 形を戻し切るまでの残りフレーム数
        bool m_scaleHeld = false;                                    // 潰した形のまま凍結している最中か
        std::uint32_t m_pendingTargetId = 0;                         // 発射する相手の永続 id

        bool m_didRebound = false;   // 直近の更新で反発を検知したか
        bool m_didBreak = false;     // 直近の更新で貫通を検知したか
        bool m_pendingBreak = false; // 保留中の結果が貫通か
        bool m_wasPeakImpact = false;
        float m_lastCharge01 = 0.0f;
        float m_lastPositionFactor = 0.0f;
        float m_lastPower = 0.0f;
        ImpactRecord m_lastImpact{};
        int m_peakFlashRemaining = 0;
        NS::Game::Player::PlayerComponent* m_movement = nullptr; // 同じ配置物の移動。非所有
        CollisionInput* m_collisionInput = nullptr;
    };
} // namespace NS::Game::Level
