#pragma once

#include "Game/Level/ImpactInputJudge.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Reflection/Curve.h"

namespace NS::Game::Player
{
    class PlayerComponent;
}

namespace NS::Game::Level
{
    class ImpactResolver;

    //! @brief 体当たりのボタン入力を読んで発動を要求する Component
    //! @details 保持はマウス左かゲームパッドの X で、ImpactInputJudge がタップ / チャージを裁く
    //! どちらも離したフレームに、溜め量を添えて PlayerComponent::RequestBodySlam を呼ぶ
    //! チャージ中は最高速度へ減速を掛ける。構えの縮みは押したフレームから掛かる
    //! 威力のチャージ倍率カーブと当たり位置係数カーブもここが持ち、ImpactResolver が参照する
    //! 依存: NS::Game::Player::PlayerComponent, NS::Obj::Curve, ImpactInputJudge, ImpactResolver
    class CollisionInput : public NS::Obj::Component
    {
    public:
        CollisionInput() noexcept;

        //! 同じ配置物の移動と裁定を引き当てる。見つからない相手に関わる処理は以後行わない
        void OnStart() override;

        //! ボタンの保持を判定へ 1 フレーム進め、発動を控えたフレームに溜め量を添えて体当たりを要求する
        void OnUpdate() override;

        //! 構えの縮みが残っていれば元の形へ戻す
        void OnEndPlay() override;

        //! 溜め量 0..1 をチャージ倍率カーブで威力の倍率にする。非有限の入力とカーブの 0 以下の値は 1 とみなす
        [[nodiscard]] float ChargeFactorFor(float charge01) const noexcept;

        //! 相手の中心からの横ずれ 0..1 を当たり位置係数カーブで威力の倍率にする。非有限の入力とカーブの 0 以下の値は 1
        //! とみなす
        [[nodiscard]] float PositionFactorFor(float offset01) const noexcept;

        //! 当たり位置係数が中心近くの当たりのしきい値以上の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsCenterHit(float positionFactor) const noexcept;

        //! チャージ中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsCharging() const noexcept { return m_judge.IsCharging(); }

        //! チャージが満タンの場合 true、それ以外の場合は false
        [[nodiscard]] bool IsChargeFull() const noexcept { return m_judge.IsChargeFull(); }

        //! 判定の実体。自動テストは実機入力を差し替えられないので、保持を直接入れる口として出す
        [[nodiscard]] ImpactInputJudge& Judge() noexcept { return m_judge; }

        NS_REFLECT_BEGIN(CollisionInput, NS::Obj::Component)
        NS_REFLECT_FIELD(m_chargeThresholdSeconds, "チャージしきい値秒")
        NS_REFLECT_FIELD(m_chargeFullSeconds, "チャージ満タン秒")
        NS_REFLECT_FIELD(m_chargeSlowRate, "チャージ減速率")
        NS_REFLECT_FIELD(m_chargeFactorCurve, "チャージ倍率カーブ")
        NS_REFLECT_FIELD(m_positionFactorCurve, "突進位置係数カーブ")
        NS_REFLECT_FIELD(m_centerHitThreshold, "中心近くの当たりのしきい値")
        NS_REFLECT_FIELD(m_chargeSquashScale, "構えの縮み")
        NS_REFLECT_FIELD(m_pressSquashScale, "押しの構えの縮み")
        NS_REFLECT_END()

    private:
        void UpdateChargeStance();

#if !defined(NS_SHIPPING)
        void DrawChargeRing();
#endif

        // どれも検証で振って探る前提の初期値
        float m_chargeThresholdSeconds = 0.2f;
        float m_chargeFullSeconds = 1.0f;
        float m_chargeSlowRate = 0.3f;
        // 既定の形は使う側が持つのが Curve の決まりなので、既定の点はコンストラクタで入れる
        NS::Obj::Curve m_chargeFactorCurve{};
        // 既定は中心直撃で 1.0、縁かすりで 0.7。画面に見えている相手の中心が狙う対象になる
        // TODO: リフレクション欄は「突進位置係数カーブ」のまま。改名すると保存済みの値が読めなくなる
        NS::Obj::Curve m_positionFactorCurve{};
        // 0.95 は既定カーブで横ずれ 0〜0.167 の区間だけが中心近くの当たりになる値
        float m_centerHitThreshold = 0.95f;
        float m_chargeSquashScale = 0.95f; // 構えと分かる最小の変化。深いと衝突の潰れ演出と紛れる
        float m_pressSquashScale = 0.97f;  // 押したフレームの反応。チャージ成立の 0.95 と見分けが付く浅さ

        ImpactInputJudge m_judge{};
        NS::Core::Vector3 m_homeScale{1.0f, 1.0f, 1.0f};
        bool m_stanceApplied = false;
        NS::Game::Player::PlayerComponent* m_movement = nullptr;
        ImpactResolver* m_resolver = nullptr;
    };
} // namespace NS::Game::Level
