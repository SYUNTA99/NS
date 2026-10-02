#pragma once

#include "Game/Entity/EntityComponent.h"
#include "Game/Player/PlayerEvents.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Reflection/Reflection.h"

class Player;

namespace NS::Obj
{
    template <typename TOwner> class StateMachine;
}

namespace NS::Game::Player
{

    //! @brief 反動の軌道のうち、当たりが決める向きと高さと距離
    //! @details 指示付き初期化で組み、Player::BeginRebound へ渡す。
    //! 上りと下りの重力と頂点の帯は Player が調整値の欄から決める
    struct ReboundArc
    {
        NS::Core::Vector3 direction{1.0f, 0.0f, 0.0f}; // 弾かれる向き。水平の成分だけを使う
        float apexHeight = 0.0f;                       // 弾かれ始めの高さから頂点までの高さ (m)
        float distance = 0.0f;                         // 弾かれ始めから同じ高さへ戻るまでの水平の距離 (m)
    };

    //! @brief 突進の記録。発動で書き、突進の間と後で読む
    //! @details 突進の状態へは持たせない。startDir と charge01 は突進が終わった後も読まれる。ResetState
    //! で全部を初期値へ戻す
    struct BodySlamRecord
    {
        float travelled = 0.0f;       // 突進で進んだ水平距離
        float distanceTarget = 0.0f;  // 突進を終える水平距離
        bool isTap = false;           // 溜め量 0 の飛び込みか
        bool justStarted = false;     // 発動したフレームか
        NS::Core::Vector3 dir{};      // 突進の水平の向き。正規化済み
        NS::Core::Vector3 startDir{}; // 最後に出した突進の、出たフレームの向き。正規化済み
        float charge01 = 0.0f;        // 発動時に確定した溜め量 0..1
        bool wasSlamming = false;     // 直前のフレームを突進中で終えたか。書くのは HandleMovement と ResetState
    };

    //! @brief 体当たりの要求と、押したフレームの狙いの控え
    struct BodySlamRequest
    {
        float bufferRemaining = 0.0f; // 出せないフレームの押しを覚える残り秒
        bool spent = false;           // 発動してから接地していないか
        float charge01 = 0.0f;        // 要求された溜め量 0..1
        NS::Core::Vector3 dir{};      // 要求に添えた出す向き。正規化済み
        bool hasDir = false;          // 要求に向きが添えてあるか
        NS::Core::Vector3 aimDir{};   // 押したフレームに控えた狙いの向き。正規化済み
        float aimAge = 0.0f;          // 狙いを控えてからの経過秒
    };

    //! @brief 突進の向きの寄せの記録
    struct HomingRecord
    {
        // 寄せた角度の累計 (度)。溜めている間は狙いの向きから、放した後は寄せる前の放す向きから測る。
        // 正の角度は +X の向きを -Z の側へ回す。突進の終わり・ResetState・立ち姿へ戻る時と、
        // 溜めている間に相手が変わった時に 0
        float angle = 0.0f;
        // 溜めている間に寄せた相手の中心と、その相手を探した角度 (度)。放す時に放す向きから測り直す
        NS::Core::Vector3 target{};
        float coneDegrees = 0.0f;
        bool hasTarget = false;
    };

    //! @brief 反動の記録
    struct ReboundRecord
    {
        NS::Core::Vector3 direction{}; // 最後に始めた反動の水平の向き。正規化済み
    };

    //! @brief 自機の能力を持つ Component
    //! @details 移動と接地は EntityComponent が持ち、ここには自機だけの記録を置く
    //! 移動の組み立て・崖つかまり・突進と反発の技は Player が持ち、ここの記録を friend 経由で読み書きする
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

        //! 持ち主の自機が基底へ預けた状態機械。持ち主が自機でなければ nullptr
        [[nodiscard]] NS::Obj::StateMachine<::Player>* States() const noexcept;

        //! 突進中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsBodySlamming() const noexcept;
        //! 突進の進み具合 0..1。突進中でなければ 0
        [[nodiscard]] float BodySlamProgress01() const noexcept;
        [[nodiscard]] float BodySlamCharge01() const noexcept { return m_slam.charge01; } //!< 発動時の溜め量 0..1
        //! 溜めた突進を終える水平の距離。欄「突進距離」の値で、単位は m
        [[nodiscard]] float BodySlamDistance() const noexcept { return Tuning().m_bodySlamDistance; }
        //! @brief 最後に出した突進の、出たフレームの水平の向きを返す
        //! @details 突進の間に寄せで曲がった分は入らない。
        //! 突進が終わった後も、次の突進を出すまで残す
        //! @return 正規化済みの向き。突進を出す前と ResetState の後はゼロ
        [[nodiscard]] NS::Core::Vector3 BodySlamStartDirection() const noexcept { return m_slam.startDir; }

        //! 反動の状態の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsRebounding() const noexcept;
        //! 最後に始めた反動の水平の向き。正規化済み。反動を始める前と ResetState の後はゼロ
        [[nodiscard]] NS::Core::Vector3 ReboundDirection() const noexcept { return m_rebound.direction; }

        //! 着地でジャンプ回数を戻し、接地中はコヨーテ猶予と突進の使用済みを戻す
        void SyncGroundState() noexcept;

        //! 自機だけの通知の受け口。基底の Events() は接地の 2 件を返すので名前を分ける
        [[nodiscard]] PlayerEvents& PlayerEventsRef() noexcept { return m_playerEvents; }

        [[nodiscard]] float RunSpeed() const noexcept { return Tuning().m_runSpeed; }

        //! 寄せた角度の累計を返す。単位は度で、正の角度は +X の向きを -Z の側へ回す
        [[nodiscard]] float HomingAngleDegrees() const noexcept { return m_homing.angle; }

        //! 丸まっている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsCurled() const noexcept { return m_curled; }

        void OnUpdate() override;

        NS_REFLECT_NONE(PlayerComponent, NS::Game::Entity::EntityComponent)

    protected:
        //! 稼働していない間もそのフレーム限りの入力は落とす。残すと再開した時に古い押下が効く
        void OnStepSkipped() override;
        //! @brief 調整値の登れる段の高さを渡して 1 フレーム動かす
        //! @details 向きを回すのは動かす前で、接地の反映と突進の距離はその後
        void HandleMovement(float dt) noexcept override;

    private:
        bool m_prevJumpHeld = false; // 前のフレームの長押し状態
        int m_jumpsRemaining = 1;    // 残りジャンプ回数
        float m_coyoteTimer = 0.0f;  // コヨーテ猶予の残り秒
        float m_bufferTimer = 0.0f;  // 先行ジャンプ入力の残り秒

        float m_maxSpeedScale = 1.0f; // 走行速度に掛ける倍率。書くのは CollisionInput

        // 丸まっているか。入れるのは CollisionInput と突進の発動、解くのは UncurlWhenSettled と縁を掴んだ時と
        // ResetState とプレイを終える時の CollisionInput
        bool m_curled = false;
        bool m_bodySlamHeld = false; // 体当たりのボタンを押しているか。書くのは CollisionInput と ResetState

        BodySlamRecord m_slam;
        BodySlamRequest m_request;
        HomingRecord m_homing;
        ReboundRecord m_rebound;

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

        NS::Obj::StateMachine<::Player>* m_states =
            nullptr; // 基底 Actor が所有する自機の状態機械 (非所有)。Player のコンストラクタが渡す

        ::Player* m_actor = nullptr; // 持ち主の自機 (非所有)。HandleMovement と ResetState が Player の技を呼ぶのに使う

        PlayerEvents m_playerEvents;
    };
} // namespace NS::Game::Player
