#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    //! @brief 押し飛ばされた配置物を落として止める簡易物理
    //! @details 自機はキネマティック制御のままで、簡易物理で動くのはこの Component を積んだ物だけ
    //! 飛んでいる間は当たりを寝かせる。張り直しは配置物の数に比例する費用なので、寝かせる時と起こす時だけ呼ぶ
    //! 地形との当たりは真下の地面探しだけで見る。壁は見ない
    //! 依存: NS::Object::ColliderComponent, NS::Object::MeshRendererComponent, NS::Physics::PhysicsWorld,
    //! NS::Object::Scene
    class LaunchedBodyComponent : public NS::Object::Component
    {
    public:
        //! @brief 発射する
        //! @details 当たりを寝かせて飛び始める。既に飛んでいる時は速度を上書きする
        //! 当たりの張り直しで確保が起きるため noexcept にしない
        //! @param[in] velocity 発射速度。非有限値は捨てて何もしない
        void Launch(const NS::Core::Vector3& velocity);

        //! 飛んでいる最中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsFlying() const noexcept { return m_flying; }

        //! 現在の速度。止まっている時は 0
        [[nodiscard]] NS::Core::Vector3 Velocity() const noexcept { return m_velocity; }

        //! 止まってから消えるまでの秒を置く。0 は消えない。非有限値と負は捨てる
        void SetRestLifeSeconds(float seconds) noexcept;

        //! 重力と摩擦を掛けて位置を進め、床の上で止める
        void OnUpdate() override;

        // 飛距離が質量と勢いの表示になる。落ち方と止まり方はプレイ中に触って詰める
        NS_REFLECT_BEGIN(LaunchedBodyComponent, NS::Object::Component)
        NS_REFLECT_FIELD(m_gravity, "重力")
        NS_REFLECT_FIELD(m_groundFriction, "接地摩擦")
        NS_REFLECT_FIELD(m_spinPerSpeed, "回転の強さ")
        NS_REFLECT_FIELD(m_restSpeed, "停止速度しきい値")
        NS_REFLECT_FIELD(m_restLifeSeconds, "止まってから消える秒")
        NS_REFLECT_END()

    private:
        // 当たりを寝かせる / 起こす。値が変わった時だけ SyncPhysics を呼ぶ
        void SetColliderActive(bool active);

        // 床へ乗せる時に使う自分の半分の高さ。当たりが無ければ既定値に描画スケールを掛ける
        [[nodiscard]] float HalfHeight() const noexcept;

        // 飛ぶ向きから回転の軸と 1 秒あたりの回転量を決める
        void BeginSpin(const NS::Core::Vector3& velocity) noexcept;

        // 回すのは空中の歩だけ
        void UpdateSpin(float dt);

        float m_gravity = -25.0f;       // 落ちる強さ
        float m_groundFriction = 6.0f;  // 接地中に水平速度を殺す強さ
        float m_spinPerSpeed = 0.5f;    // 水平の速さ 1 m/s あたりに回す速さ (ラジアン/秒)
        float m_restSpeed = 0.5f;       // これを下回ったら止まったとみなす
        float m_restLifeSeconds = 0.0f; // 止まってから消えるまでの秒。0 は消えない
        float m_restAge = 0.0f;         // 止まってからの経過秒

        NS::Core::Vector3 m_velocity{0.0f, 0.0f, 0.0f}; // 現在の速度
        NS::Core::Quaternion m_spinHome{};              // 飛び始めの姿勢。接地した歩に書き戻す
        NS::Core::Vector3 m_spinAxis{1.0f, 0.0f, 0.0f}; // 回る軸。飛ぶ向きと直交する水平方向
        float m_spinRate = 0.0f;                        // 1 秒あたりの回転 (ラジアン)
        float m_spinAngle = 0.0f;                       // 飛び始めからの回転量 (ラジアン)
        float m_halfHeight = 0.5f;                      // 飛び始めに控えた半分の高さ
        bool m_flying = false;                          // 飛んでいる最中か
        bool m_grounded = false;                        // 床に着いているか
    };
} // namespace NS::Game::Level
