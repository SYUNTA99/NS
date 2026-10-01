#pragma once

#include "Game/Entity/EntityComponent.h"
#include "Game/Player/PlayerEvents.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Reflection/Reflection.h"

class Player;

namespace NS::Game::Player
{
    class PlayerStateManager;

    //! @brief 反動の軌道のうち、当たりが決める向きと高さと距離
    //! @details 指示付き初期化で組み、PlayerComponent::BeginRebound へ渡す。
    //! 上りと下りの重力と頂点の帯は PlayerComponent が自分の欄で決める
    struct ReboundArc
    {
        NS::Core::Vector3 direction{1.0f, 0.0f, 0.0f}; // 弾かれる向き。水平の成分だけを使う
        float apexHeight = 0.0f;                       // 弾かれ始めの高さから頂点までの高さ (m)
        float distance = 0.0f;                         // 弾かれ始めから同じ高さへ戻るまでの水平の距離 (m)
    };

    //! @brief 自機の能力を持つ Component
    //! @details 移動と接地は EntityComponent が持ち、ここには自機だけの能力と条件判定を置く
    //! 状態は能力呼びの列だけにするので、状態から呼ぶ動詞は public
    class PlayerComponent : public NS::Game::Entity::EntityComponent
    {
    public:
        //! world 空間の目標移動方向と速度スケールを渡す。スケールは 0..1 に丸める
        void SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept;
        //! 直近に渡された目標速度スケール 0..1
        [[nodiscard]] float DesiredSpeedScale() const noexcept { return Input().DesiredSpeedScale(); }
        //! 直近に渡された world 空間の目標移動方向。長さは入力の強さのままで正規化されていない
        [[nodiscard]] NS::Core::Vector3 DesiredDirection() const noexcept { return Input().DesiredDirection(); }

        //! 掴まり中の生ローカル入力で各成分は -1..1。SetDesiredMove とは別に持つ
        void SetClimbMove(float localRight, float localForward) noexcept;
        [[nodiscard]] float ClimbRight() const noexcept { return Input().ClimbRight(); }
        [[nodiscard]] float ClimbForward() const noexcept { return Input().ClimbForward(); }

        //! ジャンプの押下を 1 回ぶん立てる。更新の終わりに落ちるので次のフレームには残らない
        void SetJumpPressed() noexcept;
        //! 手放しの押下を 1 回ぶん立てる。更新の終わりに落ちるので次のフレームには残らない
        void SetReleaseLedgePressed() noexcept;
        //! ジャンプボタンの長押し状態を渡す。上昇中に離すと縦速度を縮める
        void SetJumpHeld(bool held) noexcept;
        [[nodiscard]] int JumpsRemaining() const noexcept { return m_jumpsRemaining; } //!< 残りジャンプ回数

        //! @brief 走行速度に SetMaxSpeedScale の倍率を掛けた最高速度。負になる場合は 0
        //! @details 負のまま返すと EndBodySlam の頭打ちが cap / speed で負の倍率になり、突進明けに水平の向きが反転する
        [[nodiscard]] float MaxSpeed() const noexcept
        {
            const float capped = Tuning().m_runSpeed * m_maxSpeedScale;
            if (capped < 0.0f)
            {
                return 0.0f;
            }
            return capped;
        }
        //! 走行速度に掛ける倍率を渡す。非有限値は無視して直前の値を残す
        void SetMaxSpeedScale(float scale) noexcept;

        //! 奈落落ちの復活などで速度・接地・ジャンプまわりの記録と状態機械を初期状態へ戻す
        //! 丸まりも解くが根は動かさない。呼び手は先に根を出現位置へ置いてから呼ぶ
        void ResetState() noexcept;

        //! 状態が次の状態へ移る時に呼ぶ状態管理。HandleStates はこれが無いと状態を進めないので、状態の中では非 null
        [[nodiscard]] PlayerStateManager* States() const noexcept;

        //! 体当たりの発動を要求する
        //! @details 溜め量 0 はタップの飛び込みで、非有限値は 0 とみなす。
        //! そのフレームで出せない要求は先行入力時間だけ覚え、過ぎたら失効する。
        //! 1 度出すと接地するまで次は出せない。
        //! 出る向きは BodySlam が入力と押したフレームの控えから決める。前の要求に添えた向きは捨てる
        //! @param[in] charge01 溜め量 0..1
        void RequestBodySlam(float charge01) noexcept;
        //! @brief 出す向きを添えて体当たりの発動を要求する
        //! @details 溜め量と先行入力は 1 つ引数の RequestBodySlam と同じ。
        //! 出る時は入力と押したフレームの控えを見ず、添えた向きの水平を正規化した向きへ出す。
        //! 水平の長さが 0 の向きと有限でない向きは、添えなかったのと同じ
        //! @param[in] charge01 溜め量 0..1
        //! @param[in] aimDirection 出す向き。世界座標で、縦の成分は使わない
        void RequestBodySlam(float charge01, const NS::Core::Vector3& aimDirection) noexcept;
        //! 突進中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsBodySlamming() const noexcept;
        //! 突進の進み具合 0..1。突進中でなければ 0
        [[nodiscard]] float BodySlamProgress01() const noexcept;
        [[nodiscard]] float BodySlamCharge01() const noexcept { return m_bodySlamCharge01; } //!< 発動時の溜め量 0..1
        //! 溜めた突進を終える水平の距離。欄「突進距離」の値で、単位は m
        [[nodiscard]] float BodySlamDistance() const noexcept { return Tuning().m_bodySlamDistance; }
        //! 衝突の裁定と玉の回転と寄せが読む速度。突進中は向きと突進速度から作る
        //! @details 実速度は壁へ押し付けられたフレームで 0 に潰れ、衝突の先読みが今の位置から動かなくなる
        [[nodiscard]] NS::Core::Vector3 BodySlamVelocity() const noexcept;
        [[nodiscard]] NS::Core::Vector3 PredictHomingVelocity(const NS::Core::Vector3& targetCenter) const noexcept;
        void ApplyBodySlamHeading() noexcept;
        //! 突進を打ち切って通常移動へ戻す。突進中でなければ何もしない
        void CancelBodySlam() noexcept;
        //! @brief 最後に出した突進の、出たフレームの水平の向きを返す
        //! @details 突進の間に寄せで曲がった分は入らない。
        //! 突進が終わった後も、次の突進を出すまで残す
        //! @return 正規化済みの向き。突進を出す前と ResetState の後はゼロ
        [[nodiscard]] NS::Core::Vector3 BodySlamStartDirection() const noexcept { return m_bodySlamStartDir; }

        //! 向きを解決して突進を始める。向きが決まらないか距離が 0 以下の場合 false、それ以外の場合は true
        //! @details 向きは要求に添えた向き。添えていなければ AimDirection の向きに、押したフレームの控え
        //! (MarkBodySlamAim) を控えてからの秒に応じて混ぜる
        [[nodiscard]] bool BodySlam() noexcept;

        //! 突進の 1 フレームを進める。溜めた突進は水平を発動時の向きと速さで書き直し、重力を当てる
        void UpdateBodySlam(float dt) noexcept;

        //! @brief 速度を ReboundVelocityFor の値にして反動の状態へ移す
        //! @details 反動の間は上りの重力に反動の上りの重力倍率を掛け、下りは普段の重力のまま
        //! 反動の間は跳べず、空中の操作は反動中の空中の加速度だけ効く
        //! 接地していて上向きの速度が無くなったフレームに立ちへ移る
        //! @param[in] arc 弾かれる向きと頂点の高さと横の距離
        //! @return 反動を始めた場合 true、ReboundVelocityFor が 0 を返す arc で何も変えなかった場合は false
        [[nodiscard]] bool BeginRebound(const ReboundArc& arc) noexcept;
        //! @brief arc の反動を始める瞬間の速度 (m/s) を返す
        //! @details 飛ばした物の曲線と同じ式 LaunchArcInitialVelocity で出す。
        //! 上りの重力は上昇重力 × 反動の上りの重力倍率、下りは下降重力、頂点の帯は頂点滞空 Vy と頂点滞空倍率
        //! @param[in] arc 弾かれる向きと頂点の高さと横の距離
        //! @return 反動の初速。高さか距離が有限の正でない時、向きに水平の成分が無い時、
        //! 重力の欄から曲線が組めない時は 0
        [[nodiscard]] NS::Core::Vector3 ReboundVelocityFor(const ReboundArc& arc) const noexcept;
        //! 反動の状態の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsRebounding() const noexcept;
        //! 最後に始めた反動の水平の向き。正規化済み。反動を始める前と ResetState の後はゼロ
        [[nodiscard]] NS::Core::Vector3 ReboundDirection() const noexcept { return m_reboundDir; }

        // 移動の 1 フレームを作る動詞。呼ぶ順序がそのまま手触りになる
        //! 先行入力とコヨーテ猶予のタイマーを 1 フレーム進める
        void TickTimers(float dt) noexcept;
        //! @brief 入力の向きへ加速する。入力が無ければ何もしない
        //! @details 加速の上限は MaxSpeed × 倒し具合で、歩き速度を下回らない。空中は空中の加速度を使う
        void AccelerateToInputDirection(float dt) noexcept;
        //! 手を放した時の減速度で水平の速さを減らす
        void ApplyFriction(float dt) noexcept;
        //! ブレーキの減速度で水平の速さを減らす
        void ApplyBrake(float dt) noexcept;
        //! 接地かコヨーテ猶予の内で押されていれば跳ぶ
        void Jump(float dt) noexcept;
        //! 上昇中にボタンを離したフレームだけ縦速度を縮める
        void CutJumpRelease() noexcept;
        //! 上昇と下降で非対称な重力を当てる。頂点の近くは弱める
        void Gravity(float dt) noexcept;
        //! タップの飛び込みだけに当てる重力。滞空秒がタップ距離を進む秒と揃う強さにする
        void TapSlamGravity(float dt) noexcept;
        //! @brief 反動の間の重力を当てる
        //! @details 上向きの間は上昇重力に反動の上りの重力倍率を掛け、頂点の近くはさらに頂点滞空倍率を掛ける。
        //! 上向きでなければ Gravity と同じ
        void ReboundGravity(float dt) noexcept;
        //! @brief 反動の間、入力の向きへ反動中の空中の加速度で加速する。入力が無ければ何もしない
        //! @details 接地の印に依らずこの加速度を使い、入力の向きからずれた速度は減らさない
        void AccelerateDuringRebound(float dt) noexcept;
        //! 着地でジャンプ回数を戻し、接地中はコヨーテ猶予と突進の使用済みを戻す
        void SyncGroundState() noexcept;
        //! 縁を掴めるか試す。掴んだ場合 true、それ以外の場合は false。true なら呼び出し側は即 return する
        [[nodiscard]] bool LedgeGrab() noexcept;

        //! @brief 掴んでいる縁を取り直し、その高さへ位置を合わせ直す
        //! @details 重力は当てない。縁の高さが変われば追い、失ったら手を放す
        //! @return 縁が続いている場合 true、それ以外の場合は false。false なら呼び出し側は即 return する
        [[nodiscard]] bool HoldLedge() noexcept;
        //! @brief 掴まりからジャンプの縦の初速を与えて落下へ移る
        //! @return ジャンプが押された場合 true、それ以外の場合は false。true なら呼び出し側は即 return する
        [[nodiscard]] bool LedgeJump() noexcept;
        //! 左右入力で縁に沿って動く。続いていない方向へは動かない
        void Shimmy(float dt) noexcept;
        //! よじ登りを始める。2 段補間の始点と終点を決めて登りの状態へ移る
        void ClimbLedge() noexcept;
        //! よじ登りの 1 フレーム。終われば通常移動へ戻す
        void UpdateLedgeClimb(float dt) noexcept;
        //! 手を放し、その場から落下させる
        void DropLedge() noexcept;
        //! 前入力が出ている場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldClimbLedge() const noexcept;
        //! 手放しのボタンが押された場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldDropLedge() const noexcept;

        //! 接地していて、走行入力が出ているか動いている場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldWalk() const noexcept;
        //! 接地していて走行も動きも無い場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldIdle() const noexcept;
        //! 接地を外れている場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldFall() const noexcept;
        //! 接地していて上向きの速度が無い場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldLand() const noexcept;
        //! 入力の向きと水平の速度の内積がブレーキのしきい値を下回る場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldBrake() const noexcept;
        //! スティックの倒し具合が遊び以上の場合 true、それ以外の場合は false
        [[nodiscard]] bool HasMoveInput() const noexcept;
        //! 水平の速さが k_Epsilon 未満の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsStopped() const noexcept;

        //! 自機だけの通知の受け口。基底の Events() は接地の 2 件を返すので名前を分ける
        [[nodiscard]] PlayerEvents& PlayerEventsRef() noexcept { return m_playerEvents; }

        [[nodiscard]] float RunSpeed() const noexcept { return Tuning().m_runSpeed; }

        //! 押したフレームの狙いを控える。離すまでの遅れのぶん、向きを添えない発動はこの向きから始める
        void MarkBodySlamAim() noexcept;
        //! @brief 向きを添えずに要求した体当たり (タップ) を出す水平の向きを返す
        //! @details 入力・カメラの前・速度の順に見て、どれも無ければゼロ。
        //! 溜めて放した突進は、CollisionInput が狙いの線を控えていればその向きを添えるので、この向きへは出ない
        [[nodiscard]] NS::Core::Vector3 AimDirection() const noexcept;

        //! @brief 突進の向きを相手の中心へ 1 フレームぶん寄せる
        //! @details 溜めている間は chargeAim を基準に、寄せた角度の累計を目標へ近づけ、相手を控える。
        //! 控えた相手と中心が違う相手が来たら、累計を 0 から数え直す。
        //! 放す時は控えた相手を放す向きから測り直し、coneDegrees の内なら累計の大きさまでその側へ回し、外なら回さない。
        //! 溜めた突進の間は突進の向きを基準にし、累計の変化分だけ突進の向きを回す。タップの間は何もしない。
        //! 目標は基準から相手の中心への水平の角度で、突進の間はそれに累計を足す。寄せる角度の上限で切る。
        //! 累計は 1 フレームの向きの変化の上限ずつしか動かない。
        //! 基準の向きか相手への水平の向きが決まらない場合と、相手への角度が有限でない場合は何もしない
        //! @param[in] targetCenter 寄せる相手の中心。世界座標
        //! @param[in] coneDegrees
        //! 相手を探した角度。放す向きから測り直した相手を残すかどうかをこの角度で決める。単位は度
        //! @param[in] chargeAim 溜めている間に寄せた角度を測る基準の向き。縦の成分は使わない。
        //! 突進の間は突進の向きから測るので使わない
        void SteerToward(const NS::Core::Vector3& targetCenter,
                         float coneDegrees,
                         const NS::Core::Vector3& chargeAim) noexcept;
        //! 寄せた角度の累計を返す。単位は度で、正の角度は +X の向きを -Z の側へ回す
        [[nodiscard]] float HomingAngleDegrees() const noexcept { return m_homingAngle; }

        //! @brief 丸まりを入れるか解く
        //! @details 押している間は毎フレーム true が入る。自分で解くので、false はプレイを終える時だけ渡す。
        //! 丸まると当たりを球にして根を立ち姿の半長ぶん下げ、解くと立ち姿へ戻して上げる。
        //! 縁に掴まっている間とよじ登っている間の true は受けない
        void SetCurled(bool curled) noexcept;
        //! 体当たりのボタンを押しているかを渡す。押している間は丸まりを解かない
        void SetBodySlamHeld(bool held) noexcept;
        //! 丸まっている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsCurled() const noexcept { return m_curled; }

        //! 基底の OnStart に続けて、同居する状態機械を控える
        void OnStart() override;
        void OnUpdate() override;

        NS_REFLECT_NONE(PlayerComponent, NS::Game::Entity::EntityComponent)

    protected:
        //! 稼働していない間もそのフレーム限りの入力は落とす。残すと再開した時に古い押下が効く
        void OnStepSkipped() override;
        //! @brief 調整値の登れる段の高さを渡して 1 フレーム動かす
        //! @details 向きを回すのは動かす前で、接地の反映と突進の距離はその後
        void HandleMovement(float dt) noexcept override;

    private:
        [[nodiscard]] bool ComputeHomingStep(const NS::Core::Vector3& targetCenter,
                                             const NS::Core::Vector3& chargeAim,
                                             float& nextAngle) const noexcept;
        void PrepareStateStep();
        void FinishStateStep(float dt);
        //! 突進の進んだ距離を足し、距離を使い切るか進めなくなったら突進を終える
        //! @details 進めた距離は動かした後にしか出ないので、打ち切りの判定は状態でなくここに置く
        //! 受け取るのは直前の Move で実際に動いた量
        void AdvanceBodySlamTravel(const NS::Core::Vector3& delta) noexcept;
        //! 現在状態が通常移動 (立ち / 走り / 落下 / 反動) の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsLocomotion() const noexcept;
        //! @brief 丸まりを解く
        //! @details 押されていない・突進中でない・直前のフレームを突進中で終えていない・突進の予約が無い・
        //! 接地している・上向きの速度が無い、が揃ったフレームに解く。縁を掴んだ時に解くのは LedgeGrab
        void UncurlWhenSettled() noexcept;
        //! @brief 丸まりを入れるか解き、当たりの形と根の高さを一緒に切り替える
        //! @details 丸まると当たりを球にして根を立ち姿の半長ぶん下げる。
        //! 解くと立ち姿へ戻して、その時の立ち姿の半長ぶん上げる。当たりの下端 (中心 − 半長 − 半径) は動かない。
        //! 根は前フレームの位置と一緒にずらすので、描画の補間に動きとして映らない。今と同じ値なら何もしない
        void ChangeCurled(bool curled) noexcept;
        //! 突進を終える。水平の速さを MaxSpeed で切り、接地していれば走りへ、空中なら落下へ移す
        void EndBodySlam() noexcept;
        //! 寄せた角度の累計を 0 にし、溜めている間に控えた相手を捨てる
        void ForgetHoming() noexcept;
        //! @brief 溜めている間に控えた相手を放す向きから測り直し、放す向きを回す角度を返す
        //! @details 相手が放す向きから探した角度の内なら、返す角度はその側へ累計の大きさまで
        //! @param[in] releaseDir 放す水平の向き。正規化済み
        //! @return 放す向きを回す角度。単位は度で、正の角度は +X の向きを -Z の側へ回す。
        //! 控えた相手が無いか、探した角度の外か、相手への水平の向きが決まらない場合は 0
        [[nodiscard]] float HomingAngleForRelease(const NS::Core::Vector3& releaseDir) const noexcept;

        //! @brief 掴まり位置から掴める縁を探す
        //! @param[in] hangPos 手を伸ばす元になるカプセル中心の位置
        //! @param[out] outTop 見つけた縁の上端の y。見つからない場合は書き換えない
        //! @return 手の高さ以下の帯に縁があり、登り先も塞がっていない場合 true、それ以外の場合は false
        [[nodiscard]] bool FindLedgeTopAt(const NS::Core::Vector3& hangPos, float& outTop) const noexcept;

        //! 控えた狙いを今の向きにどれだけ混ぜるか 0..1。巻き戻し秒までは 1、消える秒で 0
        [[nodiscard]] float BodySlamAimBlend01() const noexcept;

        bool m_prevJumpHeld = false; // 前のフレームの長押し状態
        int m_jumpsRemaining = 1;    // 残りジャンプ回数
        float m_coyoteTimer = 0.0f;  // コヨーテ猶予の残り秒
        float m_bufferTimer = 0.0f;  // 先行ジャンプ入力の残り秒

        float m_maxSpeedScale = 1.0f; // 走行速度に掛ける倍率。書くのは CollisionInput

        // 丸まっているか。入れるのは CollisionInput と突進の発動、解くのは UncurlWhenSettled と縁を掴んだ時と
        // ResetState とプレイを終える時の CollisionInput
        bool m_curled = false;
        bool m_bodySlamHeld = false;    // 体当たりのボタンを押しているか。書くのは CollisionInput と ResetState
        bool m_wasBodySlamming = false; // 直前のフレームを突進中で終えたか。書くのは HandleMovement と ResetState

        float m_bodySlamBufferRemaining = 0.0f;                   // 出せないフレームの押しを覚える残り秒
        bool m_bodySlamSpent = false;                             // 発動してから接地していないか
        bool m_bodySlamIsTap = false;                             // 溜め量 0 の飛び込みか
        float m_bodySlamRequestCharge01 = 0.0f;                   // 要求された溜め量 0..1
        NS::Core::Vector3 m_bodySlamRequestDir{0.0f, 0.0f, 0.0f}; // 要求に添えた出す向き。正規化済み
        bool m_hasBodySlamRequestDir = false;                     // 要求に向きが添えてあるか
        float m_bodySlamCharge01 = 0.0f;                          // 発動時に確定した溜め量 0..1
        float m_bodySlamTravelled = 0.0f;                         // 突進で進んだ水平距離
        float m_bodySlamDistanceTarget = 0.0f;                    // 突進を終える水平距離
        bool m_bodySlamJustStarted = false;                       // 発動したフレームか
        NS::Core::Vector3 m_bodySlamDir{0.0f, 0.0f, 0.0f};        // 突進の水平の向き。正規化済み
        // 最後に出した突進の、出たフレームの向き。正規化済み
        NS::Core::Vector3 m_bodySlamStartDir{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 m_bodySlamAimDir{0.0f, 0.0f, 0.0f}; // 押したフレームに控えた狙いの向き。正規化済み
        float m_bodySlamAimAge = 0.0f;                        // 狙いを控えてからの経過秒
        // 寄せた角度の累計 (度)。溜めている間は狙いの向きから、放した後は寄せる前の放す向きから測る。
        // 正の角度は +X の向きを -Z
        // の側へ回す。突進の終わり・ResetState・立ち姿へ戻る時と、溜めている間に相手が変わった時に 0
        float m_homingAngle = 0.0f;
        // 溜めている間に寄せた相手の中心と、その相手を探した角度 (度)。放す時に放す向きから測り直す
        NS::Core::Vector3 m_homingTarget{0.0f, 0.0f, 0.0f};
        float m_homingTargetConeDegrees = 0.0f;
        bool m_hasHomingTarget = false;

        NS::Core::Vector3 m_facingDir{0.0f, 0.0f, 0.0f};       // 掴む向き。動こうとした水平の向きへ振り向きの速さで回る
        float m_lastMoveDistance = 0.0f;                       // 直前の Move で動いた距離。縁を探す帯の上の余白
        float m_ledgeTopY = 0.0f;                              // 掴んでいる縁の上端の y
        NS::Core::Vector3 m_ledgeFaceNormal{0.0f, 0.0f, 0.0f}; // 掴んでいる面の外向き法線
        NS::Core::Vector3 m_ledgeMantleStart{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 m_ledgeMantleEnd{0.0f, 0.0f, 0.0f};
        float m_ledgeMantleTimer = 0.0f; // よじ登りの経過秒

        friend class ::Player;
        [[nodiscard]] const NS::Obj::PlayerInput& Input() const noexcept;
        NS::Obj::PlayerInput* m_input = nullptr;
        [[nodiscard]] const PlayerParams& Tuning() const noexcept;
        const PlayerParams* m_params = nullptr;
        NS::Core::Vector3 m_reboundDir{0.0f, 0.0f, 0.0f}; // 最後に始めた反動の水平の向き。正規化済み

        PlayerStateManager* m_stateManager = nullptr; // Player Actor が所有する状態機械 (非所有)

        PlayerEvents m_playerEvents;
    };
} // namespace NS::Game::Player
