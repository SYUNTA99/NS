#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Level/ImpactInputJudge.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"

class Player;

namespace NS::Game::Player
{
    class PlayerComponent;
}

namespace NS::Game::Level
{
    //! @brief 押している間の狙いの線。狙う相手を探す線で、溜めている間に相手がいれば SlamArrow がこの線の真下の床に
    //! 矢印を貼る。溜めて放した突進はこの線の向きへ出て、溜めている間の寄せもこの線の向きから測る
    struct AimLine
    {
        NS::Core::Vector3 origin;    //!< 線を引き始める自機の位置 (配置物の根)。世界座標
        NS::Core::Vector3 direction; //!< シーンの実カメラの正面の水平の向き。正規化済みで y は 0
        float length = 0.0f;         //!< 線に沿って突進が止まる所までの距離。欄「突進距離」の値で、単位は m
    };

    //! @brief 体当たりのボタン入力を読んで発動を要求する Component
    //! @details 保持はマウス左かゲームパッドの X で、ImpactInputJudge がタップ / チャージを裁く
    //! どちらも離したフレームに、溜め量を添えて PlayerComponent::RequestBodySlam を呼ぶ。
    //! 溜めて放した時は、放す前のフレームに控えた狙いの線の向きも添える
    //! チャージ中は最高速度へ減速を掛ける。構えの縮みと自機の丸まりは押したフレームから掛かる
    //! 依存: NS::Game::Player::PlayerComponent, NS::Obj::Curve, ImpactInputJudge, ImpactResolver, HitTier
    class CollisionInput : public NS::Obj::Component
    {
    public:
        CollisionInput() noexcept;

        //! 同じ配置物の移動と裁定を引き当てる。見つからない相手に関わる処理は以後行わない
        void OnStart() override;

        //! @brief ボタンの保持を判定へ 1 フレーム進め、発動を控えたフレームに溜め量を添えて体当たりを要求する
        //! @details 溜めて放したフレームは、狙いの線を控えていればその向きも添える。
        //! 押している間はカメラの正面の線で狙う相手を探して控える。押している間と突進の間は、線の上の相手か、
        //! 基準の向き (押している間は狙いの線の向き、突進の間は突進の向き) の前方の近い相手へ突進の向きを寄せる
        void OnUpdate() override;
        //! @brief Observe・AdvanceState・ApplyControl を続けて呼び、判定を 1 フレーム進める
        //! @details 突進の向きは寄せるが、移動の速度へは入れ直さない
        //! @param[in] held ボタンを押しているか
        //! @param[in] dt 進める秒
        void Step(bool held, float dt);
        //! @brief マウス左かゲームパッドの X を押しているかを読む
        //! @details マウス左は、ゲームがマウスのボタンを受け取っている間だけ数える
        //! @return どちらかを押している場合 true、それ以外の場合は false
        [[nodiscard]] bool ReadHeld() const;
        //! @brief このフレームの押しと突進中かを控え、狙う相手と寄せる相手を探す
        //! @details 控えた値は AdvanceState と ApplyControl が 1 回ずつ使う
        //! @param[in] held ボタンを押しているか
        void Observe(bool held);
        //! @brief Observe で控えた押しで溜めを 1 フレーム進める
        //! @details 持ち主が Player なら Player の溜めを進める。Observe の後に 1 回だけ効き、2 回目は何もしない
        //! @param[in] dt 進める秒
        void AdvanceState(float dt);
        //! @brief 突進中なら突進の向きを狙う相手へ寄せる
        //! @details AdvanceState の後に 1 回だけ効き、AdvanceState より先に呼んだ時と 2 回目は何もしない
        //! @param[in] refreshVelocity 寄せた向きを移動の速度へ入れ直すか。移動が休止中なら入れ直さない
        void ApplyControl(bool refreshVelocity = true);
        //! @brief 次の固定ステップの突進の速度を見込みで返す
        //! @details Observe の時に突進中で寄せる相手がいれば、その相手へ寄せた速度を返す
        //! @return 見込みの速度。同じ配置物に移動が無ければ 0
        [[nodiscard]] NS::Core::Vector3 PredictedSlamVelocity() const noexcept;

        //! 構えの縮みが残っていれば元の形へ戻し、自機へ渡した押しの印を戻して丸まりを解く
        void OnEndPlay() override;

        //! 溜め量 0..1 をチャージ倍率カーブで威力の倍率にする。非有限の入力とカーブの 0 以下の値は 1 とみなす
        [[nodiscard]] float ChargeFactorFor(float charge01) const noexcept;

        //! 相手の中心からの横ずれ 0..1 を当たり位置係数カーブで威力の倍率にする。非有限の入力とカーブの 0 以下の値は 1
        //! とみなす
        [[nodiscard]] float PositionFactorFor(float offset01) const noexcept;

        //! 相手の中心からの横ずれ 0..1 を段に分ける。境目ちょうどは外側の段とし、非有限の入力は大きな外れとみなす
        [[nodiscard]] HitTier HitTierFor(float offset01) const noexcept;

        //! 溜め中に最高速へ掛ける倍率を返す。1 − チャージ減速率を 0..1 に丸める
        [[nodiscard]] float ChargingSpeedScale() const noexcept;

        //! チャージ中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsCharging() const noexcept { return m_judge.IsCharging(); }

        //! チャージが満タンの場合 true、それ以外の場合は false
        [[nodiscard]] bool IsChargeFull() const noexcept { return m_judge.IsChargeFull(); }

        //! @brief 押している間に控えた狙う相手を読む
        //! @details 押している間は毎フレーム、TryGetAimLine の狙いの線の向きと長さで
        //! ImpactResolver::FindSlamLineTarget を呼び、線を進む自機の縁が突進が止まる所までに触れる相手を探して控える。
        //! 押していないフレームと、狙いの線が無いフレームは控えを消す
        //! @param[out] outTarget 控えた狙う相手。控えが無い場合は書き換えない
        //! @return 控えがある場合 true、それ以外の場合は false
        [[nodiscard]] bool TryGetAimTarget(SlamLineTarget& outTarget) const noexcept;

        //! @brief 押している間に控えた狙いの線を読む
        //! @details 押している間は毎フレーム、自機の位置からシーンの実カメラの正面の水平の向き
        //! (NS::Obj::CameraComponent::ForwardHorizontal) へ、PlayerComponent::BodySlamDistance の長さの線を控える。
        //! 押したキーとスティックの向きは使わない。狙う相手がいなくても控える。
        //! 押していないフレーム、所属シーンか実カメラが無いフレーム、正面の向きが決まらない (水平の長さが 0 か
        //! 有限でない) フレームは控えを消す
        //! @param[out] outLine 控えた狙いの線。控えが無い場合は書き換えない
        //! @return 控えがある場合 true、それ以外の場合は false
        [[nodiscard]] bool TryGetAimLine(AimLine& outLine) const noexcept;

        //! 判定の実体。自動テストは実機入力を差し替えられないので、保持を直接入れる口として出す
        [[nodiscard]] ImpactInputJudge& Judge() noexcept { return m_judge; }
        //! 判定の実体を読むだけの口。PlayerAppearance が溜め量を読む
        [[nodiscard]] const ImpactInputJudge& Judge() const noexcept { return m_judge; }

        NS_REFLECT_NONE(CollisionInput, NS::Obj::Component)

    private:
        void UpdateChargeStance();
        // 押している間はカメラの正面へ狙いの線を作って控え、その線で狙う相手を探して控える。
        // 押していなければ両方の控えを消す
        void UpdateAimTarget(bool held);
        void ObserveHomingTarget(bool held);
        // 押している間は狙う相手、突進の間は突進の向きの線の上の相手が、寄せの角度と距離の内に居ればそれへ、
        // 居なければ基準の向きの前方で一番近い相手へ突進の向きを寄せる。
        // 基準の向きは、押している間は狙いの線の向き (線が無ければ AimDirection)、突進の間は突進の向き
        void SteerTowardTarget();

#if !defined(NS_SHIPPING)
        void DrawChargeRing();
#endif

        friend class ::Player;
        void AdvanceCharge(bool held, float dt);
        ::Player* m_player = nullptr;
        [[nodiscard]] const NS::Game::Player::PlayerParams& Tuning() const noexcept;
        const NS::Game::Player::PlayerParams* m_params = nullptr;

        ImpactInputJudge m_judge{};
        AimLine m_observedAimLine{};
        SlamLineTarget m_observedAimTarget{};
        NS::Core::Vector3 m_observedHomingCenter{};
        NS::Core::Vector3 m_observedHomingForward{};
        bool m_observedHasAimLine = false;
        bool m_observedHasAimTarget = false;
        bool m_observedHasHomingTarget = false;
        bool m_observedRushing = false;
        bool m_observedHeld = false;
        bool m_stateReady = false;
        bool m_controlReady = false;
        SlamLineTarget m_aimTarget{}; // 押している間の狙う相手。m_hasAimTarget が偽の間は読まない
        bool m_hasAimTarget = false;
        AimLine m_aimLine{}; // 押している間の狙いの線。m_hasAimLine が偽の間は読まない
        bool m_hasAimLine = false;
        NS::Core::Vector3 m_homeScale{1.0f, 1.0f, 1.0f};
        bool m_stanceApplied = false;
        NS::Game::Player::PlayerComponent* m_movement = nullptr;
        ImpactResolver* m_resolver = nullptr;
    };
} // namespace NS::Game::Level
