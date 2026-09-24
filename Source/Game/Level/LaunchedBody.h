#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"

namespace NS::Obj
{
    class RigidBody;
}

namespace NS::Game::Level
{
    //! @brief 押し飛ばされて転がり、止まるか壊れるまでを受け持つ Component
    //! @details body は自分で作らず、同じ配置物の RigidBody を動かす
    //! 置かれている間と止まった後は RigidBody をキネマティックにし、飛んでいる間だけダイナミックにする
    //! 質量・摩擦・跳ね返りは RigidBody が持ち、ここは回転の強さ・止まってから消えるまで・破片の設定だけを持つ
    //! RigidBody が無い配置物は、飛ばす時にキネマティックの RigidBody を足してから飛ばす
    //! 依存: NS::Obj::RigidBody, NS::Obj::Collider, Breakable
    class LaunchedBody : public NS::Obj::Component
    {
    public:
        // 物理の更新が済んでから body を読むので LateUpdate 帯で名乗る
        LaunchedBody() noexcept;

        //! @brief 同じ配置物の RigidBody をダイナミックにして velocity で飛ばす
        //! @details 前転の角速度も入れる。RigidBody が無ければ足す。body の生成で確保が起きるので noexcept にしない
        void Launch(const NS::Core::Vector3& velocity);

        [[nodiscard]] bool IsFlying() const noexcept { return m_flying; }

        //! 飛んでいる間の線速度。飛んでいない間は 0
        [[nodiscard]] NS::Core::Vector3 Velocity() const;

        void SetRestLifeSeconds(float seconds) noexcept;

        void SetDebrisCount(int count) noexcept;

        // ObjectList からは消さない。更新の最中に消すと集めた並びに解放済みのポインタが残る
        void Shatter();

        void OnUpdate() override;

        void OnEndPlay() override;

        NS_REFLECT_BEGIN(LaunchedBody, NS::Obj::Component)
        NS_REFLECT_FIELD(m_spinPerSpeed, "回転の強さ")
        NS_REFLECT_FIELD(m_restLifeSeconds, "止まってから消える秒")
        NS_REFLECT_FIELD(m_debrisCount, "破片の数")
        NS_REFLECT_FIELD(m_debrisSpeed, "破片の速さ")
        NS_REFLECT_FIELD(m_debrisLifeSeconds, "破片の寿命秒")
        NS_REFLECT_FIELD(m_debrisScale, "破片の大きさ")
        NS_REFLECT_FIELD(m_debrisBaseColor, "破片の色")
        NS_REFLECT_END()

    private:
        [[nodiscard]] NS::Core::Vector3 TumbleFrom(const NS::Core::Vector3& velocity) const noexcept;

        // 同じ配置物の RigidBody。無ければキネマティックで足し、collider を形として取り込ませて body を作る
        // 持ち主が Scene に居なければ null
        [[nodiscard]] NS::Obj::RigidBody* EnsureRigidBody();

        // 止まった所でキネマティックへ戻す。次に飛ばされるまでその場の当たりとして残る
        void ComeToRest();

        // 当たりごと消す。RigidBody と collider の body を外し、描画と更新も止める
        void HideAndSleep();

        float m_spinPerSpeed = 0.5f;
        float m_restLifeSeconds = 0.0f;
        float m_restAge = 0.0f;
        int m_debrisCount = 5;      // 壊れた時に出す破片の数
        float m_debrisSpeed = 6.0f; // 質量 1 の物が壊れた時の破片の水平初速
        // 破片が止まってから消えるまでの秒。押し飛ばした配置物と違い破片は残さない
        float m_debrisLifeSeconds = 8.0f;
        // 破片 1 個のスケール。壊れた物より明確に小さくして、数で壊れた量を見せる
        float m_debrisScale = 0.25f;
        NS::Core::Vector3 m_debrisBaseColor{0.35f, 0.32f, 0.30f};

        bool m_flying = false;
    };
} // namespace NS::Game::Level
