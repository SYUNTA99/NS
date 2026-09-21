#pragma once

#include "Game/Entity/EntityComponent.h"
#include "Game/Player/PlayerEvents.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Reflection/Reflection.h"

namespace NS::Game::Entity
{
    class EntityStateManagerComponent;
}

namespace NS::Game::Player
{
    class PlayerStateManagerComponent;

    //! @brief 自機の能力を持つ Component
    //! @details 移動と接地は EntityComponent が持ち、ここには自機だけの能力と条件判定を置く
    //! 調整値は自分の欄として持つ。Inspector とシーン JSON はこの欄をリフレクション越しに読み書きする
    //! 状態は能力呼びの列だけにするので、状態から呼ぶ動詞は public
    //! 依存: NS::Game::Entity::EntityComponent / EntityStateManagerComponent
    class PlayerComponent : public NS::Game::Entity::EntityComponent
    {
    public:
        //! world 空間の目標移動方向と速度スケールを渡す。スケールは 0..1 に丸める
        void SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept;
        //! 直近に渡された目標速度スケール 0..1
        [[nodiscard]] float DesiredSpeedScale() const noexcept { return m_desiredSpeedScale; }
        //! 直近に渡された world 空間の目標移動方向。長さは入力の強さのままで正規化されていない
        [[nodiscard]] NS::Core::Vector3 DesiredDirection() const noexcept { return m_desiredDir; }

        //! 掴まり中の生ローカル入力で各成分は -1..1。SetDesiredMove とは別に持つ
        void SetClimbMove(float localRight, float localForward) noexcept;
        [[nodiscard]] float ClimbRight() const noexcept { return m_climbRight; }     //!< 掴まり中の左右入力
        [[nodiscard]] float ClimbForward() const noexcept { return m_climbForward; } //!< 掴まり中の前後入力

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
            const float capped = m_runSpeed * m_maxSpeedScale;
            if (capped < 0.0f)
            {
                return 0.0f;
            }
            return capped;
        }
        //! 走行速度に掛ける倍率を渡す。非有限値は無視して直前の値を残す
        void SetMaxSpeedScale(float scale) noexcept;

        //! 奈落落ちの復活などで速度・接地・ジャンプまわりの記録と状態機械を初期状態へ戻す
        void ResetState() noexcept;

        //! 状態が次の状態へ移る時に呼ぶ状態管理。HandleStates はこれが無いと状態を進めないので、状態の中では非 null
        [[nodiscard]] NS::Game::Entity::EntityStateManagerComponent* States() const noexcept;

        //! 体当たりの発動を要求する
        //! @details 溜め量 0 はタップの飛び込みで、非有限値は 0 とみなす。
        //! そのフレームで出せない要求は先行入力時間だけ覚え、過ぎたら失効する。
        //! 1 度出すと接地するまで次は出せない
        //! @param[in] charge01 溜め量 0..1
        void RequestBodySlam(float charge01) noexcept;
        //! 突進中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsBodySlamming() const noexcept;
        //! 突進の進み具合 0..1。突進中でなければ 0
        [[nodiscard]] float BodySlamProgress01() const noexcept;
        [[nodiscard]] float BodySlamCharge01() const noexcept { return m_bodySlamCharge01; } //!< 発動時の溜め量 0..1
        //! 衝突の裁定が読む速度。突進中は向きと突進速度から作る
        //! @details 実速度は壁へ押し付けられたフレームで 0 に潰れ、衝突の先読みが今の位置から動かなくなる
        [[nodiscard]] NS::Core::Vector3 BodySlamVelocity() const noexcept;
        //! 突進を打ち切って通常移動へ戻す。突進中でなければ何もしない
        void CancelBodySlam() noexcept;

        //! 向きを解決して突進を始める。向きが決まらないか距離が 0 以下の場合 false、それ以外の場合は true
        //! @details 向きは 入力の水平 → カメラの水平前方 → 現在速度の水平 の順で解決する
        [[nodiscard]] bool BodySlam() noexcept;

        //! 突進の 1 フレームを進める。溜めた突進は水平を発動時の向きと速さで書き直し、重力を当てる
        void UpdateBodySlam(float dt) noexcept;

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
        //! 入力の向きと水平の速度の内積がブレーキのしきい値を下回る場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldBrake() const noexcept;
        //! スティックの倒し具合が遊び以上の場合 true、それ以外の場合は false
        [[nodiscard]] bool HasMoveInput() const noexcept;
        //! 水平の速さが k_Epsilon 未満の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsStopped() const noexcept;

        //! 自機だけの通知の受け口。基底の Events() は接地の 2 件を返すので名前を分ける
        [[nodiscard]] PlayerEvents& PlayerEventsRef() noexcept { return m_playerEvents; }

        [[nodiscard]] float RunSpeed() const noexcept { return m_runSpeed; } //!< 走行の最高速度

        //! 押したフレームの狙いを控える。離すまでの遅れのぶん、発動はこの向きから始める
        void MarkBodySlamAim() noexcept;

        //! 基底の OnStart に続けて、同居する状態機械を控える
        void OnStart() override;

        // 欄は登録される具象型に置く。リフレクションの直列化は自分の型の欄だけを回り、基底の鎖はたどらない
        NS_REFLECT_BEGIN(PlayerComponent, NS::Game::Entity::EntityComponent)
        NS_REFLECT_FIELD(m_jumpImpulse, "ジャンプ初速")
        NS_REFLECT_FIELD(m_gravityUp, "上昇重力")
        NS_REFLECT_FIELD(m_gravityDown, "下降重力")
        NS_REFLECT_FIELD(m_apexHangVy, "頂点滞空 Vy")
        NS_REFLECT_FIELD(m_apexHangScale, "頂点滞空倍率")
        NS_REFLECT_FIELD(m_jumpReleaseScale, "ジャンプ離し倍率")
        NS_REFLECT_FIELD(m_coyoteTime, "コヨーテ時間")
        NS_REFLECT_FIELD(m_jumpBufferTime, "先行入力時間")
        NS_REFLECT_FIELD(m_walkSpeed, "歩き速度")
        NS_REFLECT_FIELD(m_runSpeed, "走行速度")
        NS_REFLECT_FIELD(m_acceleration, "加速度")
        NS_REFLECT_FIELD(m_airAcceleration, "空中の加速度")
        NS_REFLECT_FIELD(m_turningDrag, "曲がる時の抵抗")
        NS_REFLECT_FIELD(m_friction, "手を放した時の減速度")
        NS_REFLECT_FIELD(m_deceleration, "ブレーキの減速度")
        NS_REFLECT_FIELD(m_brakeThreshold, "ブレーキのしきい値")
        NS_REFLECT_FIELD(m_stickDeadzone, "スティック遊び")
        NS_REFLECT_FIELD(m_maxStepHeight, "登れる段の高さ")
        NS_REFLECT_FIELD(m_ledgeGrabBelowHand, "掴める縁の下向き距離")
        NS_REFLECT_FIELD(m_ledgeReach, "縁へ手を伸ばす距離")
        NS_REFLECT_FIELD(m_ledgeClimbDuration, "よじ登りの所要時間")
        NS_REFLECT_FIELD(m_ledgeShimmySpeed, "縁の横移動速度")
        NS_REFLECT_FIELD(m_turnSpeed, "振り向きの速さ")
        NS_REFLECT_FIELD(m_bodySlamSpeed, "突進速度")
        NS_REFLECT_FIELD(m_bodySlamDistance, "突進距離")
        NS_REFLECT_FIELD(m_tapSlamSpeed, "タップ初速")
        NS_REFLECT_FIELD(m_tapSlamUpSpeed, "タップの上向き初速")
        NS_REFLECT_FIELD(m_tapSlamDistance, "タップ距離")
        NS_REFLECT_FIELD(m_slamAimHoldTime, "狙いの巻き戻し秒")
        NS_REFLECT_FIELD(m_slamAimFadeTime, "狙いの巻き戻しが消える秒")
        NS_REFLECT_END()

    protected:
        //! 1 フレームの中身。状態機械を 1 つ進め、末尾でそのフレーム限りの入力を落とす
        void HandleStates(float dt) override;
        //! 稼働していない間もそのフレーム限りの入力は落とす。残すと再開した時に古い押下が効く
        void OnStepSkipped() override;
        //! @brief 調整値の登れる段の高さを渡して 1 フレーム動かす
        //! @details 向きを回すのは動かす前で、接地の反映と突進の距離はその後
        void HandleMovement(float dt) noexcept override;

    private:
        //! 突進の進んだ距離を足し、距離を使い切るか進めなくなったら突進を終える
        //! @details 進めた距離は動かした後にしか出ないので、打ち切りの判定は状態でなくここに置く
        //! 受け取るのは直前の Move で実際に動いた量
        void AdvanceBodySlamTravel(const NS::Core::Vector3& delta) noexcept;
        //! 現在状態が通常移動 (立ち / 走り / 落下) の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsLocomotion() const noexcept;
        //! 突進を終える。水平の速さを MaxSpeed で切り、接地していれば走りへ、空中なら落下へ移す
        void EndBodySlam() noexcept;

        //! @brief 掴まり位置から掴める縁を探す
        //! @param[in] hangPos 手を伸ばす元になるカプセル中心の位置
        //! @param[out] outTop 見つけた縁の上端の y。見つからない場合は書き換えない
        //! @return 手の高さ以下の帯に縁があり、登り先も塞がっていない場合 true、それ以外の場合は false
        [[nodiscard]] bool FindLedgeTopAt(const NS::Core::Vector3& hangPos, float& outTop) const noexcept;

        //! 体当たりを出す水平の向き。入力・カメラの前・速度の順に見て、どれも無ければゼロ
        [[nodiscard]] NS::Core::Vector3 AimDirection() const noexcept;
        //! 控えた狙いを今の向きにどれだけ混ぜるか 0..1。巻き戻し秒までは 1、消える秒で 0
        [[nodiscard]] float BodySlamAimBlend01() const noexcept;

        NS::Core::Vector3 m_desiredDir{0.0f, 0.0f, 0.0f}; // 入力から作る world 空間の目標移動方向
        float m_desiredSpeedScale = 0.0f;                 // 目標速度スケール 0..1
        float m_climbRight = 0.0f;                        // 掴まり中の左右入力 -1..1
        float m_climbForward = 0.0f;                      // 掴まり中の前後入力 -1..1

        bool m_jumpHeld = false;                     // ジャンプボタン長押し中か
        bool m_prevJumpHeld = false;                 // 前のフレームの長押し状態
        bool m_jumpPressedThisFrame = false;         // このフレームでジャンプ押下があったか
        bool m_releaseLedgePressedThisFrame = false; // このフレームで手放しの押下があったか
        int m_jumpsRemaining = 1;                    // 残りジャンプ回数
        float m_coyoteTimer = 0.0f;                  // コヨーテ猶予の残り秒
        float m_bufferTimer = 0.0f;                  // 先行ジャンプ入力の残り秒

        float m_maxSpeedScale = 1.0f; // 走行速度に掛ける倍率。書くのは CollisionInputComponent

        float m_bodySlamBufferRemaining = 0.0f;               // 出せないフレームの押しを覚える残り秒
        bool m_bodySlamSpent = false;                         // 発動してから接地していないか
        bool m_bodySlamIsTap = false;                         // 溜め量 0 の飛び込みか
        float m_bodySlamRequestCharge01 = 0.0f;               // 要求された溜め量 0..1
        float m_bodySlamCharge01 = 0.0f;                      // 発動時に確定した溜め量 0..1
        float m_bodySlamTravelled = 0.0f;                     // 突進で進んだ水平距離
        float m_bodySlamDistanceTarget = 0.0f;                // 突進を終える水平距離
        bool m_bodySlamJustStarted = false;                   // 発動したフレームか
        NS::Core::Vector3 m_bodySlamDir{0.0f, 0.0f, 0.0f};    // 突進の水平の向き。正規化済み
        NS::Core::Vector3 m_bodySlamAimDir{0.0f, 0.0f, 0.0f}; // 押したフレームに控えた狙いの向き。正規化済み
        float m_bodySlamAimAge = 0.0f;                        // 狙いを控えてからの経過秒

        NS::Core::Vector3 m_facingDir{0.0f, 0.0f, 0.0f};       // 掴む向き。動こうとした水平の向きへ振り向きの速さで回る
        float m_lastMoveDistance = 0.0f;                       // 直前の Move で動いた距離。縁を探す帯の上の余白
        float m_ledgeTopY = 0.0f;                              // 掴んでいる縁の上端の y
        NS::Core::Vector3 m_ledgeFaceNormal{0.0f, 0.0f, 0.0f}; // 掴んでいる面の外向き法線
        NS::Core::Vector3 m_ledgeMantleStart{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 m_ledgeMantleEnd{0.0f, 0.0f, 0.0f};
        float m_ledgeMantleTimer = 0.0f; // よじ登りの経過秒

        float m_jumpImpulse = 12.0f;     // ジャンプ初速
        float m_gravityUp = -25.0f;      // 上昇中の重力
        float m_gravityDown = -35.0f;    // 下降中の重力、上昇より強い
        float m_apexHangVy = 1.0f;       // 頂点とみなす縦速度のしきい値
        float m_apexHangScale = 0.5f;    // 頂点付近で重力に掛ける倍率
        float m_jumpReleaseScale = 0.6f; // 上昇中に離した時の縦速度倍率
        // 接地を離れてもジャンプを受ける猶予秒。実機プレイで詰めた約 1.5 フレームで、踏み外し直後のごく短い救済だけ残す
        float m_coyoteTime = 0.025f;
        // 着地前の先行ジャンプ入力を覚える秒。体当たりの先行入力と共用で、分けない
        // TODO: 暫定値。人の早押し誤差は概ね 100ms なので目標は 0.1 秒、体感で詰める
        float m_jumpBufferTime = 0.25f;
        float m_walkSpeed = 4.0f; // 歩き速度
        float m_runSpeed = 8.0f;  // 走行の最高速度
        // 入力の向きへの加速度 (m/s²)。0 から走り 8 m/s まで 0.2 秒 (12 フレーム) で、手を放して止まるまでと同じ
        float m_acceleration = 40.0f;
        // 空中で入力の向きへ足す加速度 (m/s²)。前は空中も地上と同じ時定数で近づけていたので、地上と同じ強さから始める
        float m_airAcceleration = 40.0f;
        // 入力の向きからずれた速度を減らす減速度 (m/s²)
        // 前は向きの成分もずれた成分も 1 本の式でまとめて近づけていたので、加速度と同じ強さから始める
        float m_turningDrag = 40.0f;
        // 手を放した時の減速度 (m/s²)。走り 8 m/s から 0.2 秒 (12 フレーム) で止まる
        float m_friction = 40.0f;
        // ブレーキの減速度 (m/s²)。手を放した時と同じ強さ
        float m_deceleration = 40.0f;
        // 入力の向きと速度の内積のしきい値 (m/s)。逆向きに 0.8 m/s より速く動いている時にブレーキ
        float m_brakeThreshold = -0.8f;
        float m_stickDeadzone = 0.3f; // スティック入力のデッドゾーン
        // 走ったまま登れる段の高さ。実寸の階段 1 段 (15〜20 cm) は越え、半マス (50 cm) はジャンプが要る
        float m_maxStepHeight = 0.25f;

        // カプセルの円柱部の上端を手とみなし、ブロック上端が手からこの距離だけ下までにあれば掴める
        float m_ledgeGrabBelowHand = 0.5f;
        float m_ledgeReach = 0.3f; // カプセル表面から前方へ手を伸ばす追加距離
        // ぶら下がりから上面へよじ登る所要時間。瞬間移動を避けて登りを視認できるようにする
        float m_ledgeClimbDuration = 0.25f;
        float m_ledgeShimmySpeed = 2.0f; // 縁に沿った左右移動の速度
        // 掴む向きを動く向きへ回す速さ (度/秒)。180° を約 0.19 秒で回る
        // 手放した直後に壁の方へ入力しても、振り向く前に手が縁より下へ落ちて掴み直さない
        float m_turnSpeed = 970.0f;

        // どれも触って決める仮値
        float m_bodySlamSpeed = 20.0f;
        float m_bodySlamDistance = 10.0f;
        float m_tapSlamSpeed = 10.0f;
        float m_tapSlamUpSpeed = 3.0f;
        // 2.5 では目の前の物にしか届かず、狙って押す価値が無かった。実機で 2.5 倍にして詰める
        // 初速はそのままなので踏み込みは 0.25 秒から 0.625 秒へ延びる
        float m_tapSlamDistance = 6.25f;
        // 押したフレームに控えた狙いをそのまま使う秒と、今の向きへ戻し切る秒。押してから離すまでにカメラが振れると
        // 飛ぶ先がずれる。離すまでの遅れの平均 0.15 秒はこの 2 つの間に入る。外の実測をそのまま借りた出発点で、
        // NS では測っていない
        float m_slamAimHoldTime = 0.11f;
        float m_slamAimFadeTime = 0.19f;

        PlayerStateManagerComponent* m_stateManager = nullptr; // 状態機械 (非所有)

        PlayerEvents m_playerEvents;
    };
} // namespace NS::Game::Player
