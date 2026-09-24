#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>

#include <vector>

namespace NS::Obj
{
    //! @brief 同じ object の collider をまとめて 1 つの動く body にし、物理で動かす Component
    //! @details 形は同じ object の稼働中の collider から集める。子の object の collider は集めない
    //! 箱・球・カプセルは 1 つの body の合成形状の部品になり、その collider は自分の body を持たない
    //! トリガーの箱は自分の sensor body のまま、物体が動いたフレームに追従する
    //! メッシュとスロープは動く body の形にできない。静的な body のまま元の場所に残り、同期のたびに警告を出す
    //! body の原点は持ち主の世界の位置と回転で、拡縮は形の寸法へ焼き込む
    //! 形と置き場所を作り直すのは SyncToPhysics だけ。拡縮や collider の寸法を変えたら ObjectList::SyncPhysics で張り直す
    //! ダイナミックは Scene が物理を 1 歩進めた後に、body の姿勢を持ち主の Transform へ書き戻す
    //! Transform を直接書いても次の 1 歩で上書きされるので、置き直しは Teleport を使う
    //! キネマティックは物理の 1 歩の前に Transform の姿勢へ body を運び、途中で触れた相手を押す
    //! 重力は物体ごとに向きと強さを持つ。PhysicsScene の世界の重力は受けない
    //! 物理の 1 歩の前に質量 × 重力の力を掛けるので、質量に依らず同じ速さで落ちる。眠っている物には掛けない
    //! 依存: Collider, NS::Phys::PhysicsScene
    class RigidBody : public Component
    {
    public:
        //! @brief 同じ object の collider から形を集め、持ち主の姿勢へ body を置き直す。body が無ければ作る
        //! @details 置き直しは瞬間移動で、速度はそのまま残る
        //! 集まる形が無ければ body を外して警告する
        //! 持ち主が Scene に居ない時と、持ち主の Scene 以外の PhysicsScene を渡された時は、エラーを出して受け取らない
        void SyncToPhysics(NS::Phys::PhysicsScene& physics);

        //! 自分の body を physics から外す。無ければ何もしない。受け取る条件は SyncToPhysics と同じ
        void RemoveFromPhysics(NS::Phys::PhysicsScene& physics);

        //! 動く body の id。まだ作っていないか、集まる形が無ければ無効
        [[nodiscard]] JPH::BodyID BodyId() const noexcept { return m_bodyId; }

        //! @brief Scene が物理を 1 歩進める直前に呼ぶ
        //! @details 欄の変化を body へ映し、キネマティックなら Transform の姿勢へ body を運ぶ
        void PrePhysicsStep();
        //! @brief Scene が物理を 1 歩進めた直後に呼ぶ
        //! @details ダイナミックなら body の姿勢を Transform へ書き戻し、追従する collider を動かす
        void PostPhysicsStep();

        //! Scene に入った時に body を作る。実行中に湧いた一時オブジェクトは ObjectList::SyncPhysics を通らない
        void OnStart() override;
        //! 配置物ごと消える前に、持ち主の Scene の PhysicsScene から body を外す
        void OnEndPlay() override;

        //! 力を受けず Transform の姿勢へ運ばれるか
        void SetKinematic(bool kinematic) noexcept { m_kinematic = kinematic; }
        [[nodiscard]] bool IsKinematic() const noexcept { return m_kinematic; }
        //! 質量 (kg)。0 以下は body へ入れる時に 1 として扱う
        void SetMass(float mass) noexcept { m_mass = mass; }
        [[nodiscard]] float Mass() const noexcept { return m_mass; }
        //! 重力を受けるか
        void SetUseGravity(bool useGravity) noexcept { m_useGravity = useGravity; }
        [[nodiscard]] bool UsesGravity() const noexcept { return m_useGravity; }
        //! この物体の重力の加速度 (m/s^2)。向きも持つので横や上にも引ける。非有限の成分を含む値は受け取らない
        void SetGravity(const NS::Core::Vector3& gravity) noexcept;
        [[nodiscard]] const NS::Core::Vector3& Gravity() const noexcept { return m_gravity; }
        //! 摩擦と跳ね返り。負は body へ入れる時に 0 として扱う
        void SetFriction(float friction) noexcept { m_friction = friction; }
        [[nodiscard]] float Friction() const noexcept { return m_friction; }
        void SetRestitution(float restitution) noexcept { m_restitution = restitution; }
        [[nodiscard]] float Restitution() const noexcept { return m_restitution; }
        //! 移動と回転の減衰。負は body へ入れる時に 0 として扱う
        void SetLinearDamping(float damping) noexcept { m_linearDamping = damping; }
        [[nodiscard]] float LinearDamping() const noexcept { return m_linearDamping; }
        void SetAngularDamping(float damping) noexcept { m_angularDamping = damping; }
        [[nodiscard]] float AngularDamping() const noexcept { return m_angularDamping; }
        //! 速い物がすり抜けないよう、1 歩で動いた道を掃引して当てるか
        void SetContinuousCollision(bool continuous) noexcept { m_continuousCollision = continuous; }
        [[nodiscard]] bool IsContinuousCollision() const noexcept { return m_continuousCollision; }
        //! 世界の各軸に沿った移動を止める。6 軸全部を止めるとキネマティックとして扱う
        void LockPosition(bool x, bool y, bool z) noexcept;
        //! 世界の各軸まわりの回転を止める
        void LockRotation(bool x, bool y, bool z) noexcept;

        //! 欄から作った body の動き方。body へ入れる前の値で、PhysicsScene が直す値はそのまま
        [[nodiscard]] NS::Phys::BodyMotion Motion() const noexcept;

        //! 線速度 (m/s)。body が無ければ 0
        [[nodiscard]] NS::Core::Vector3 Velocity() const;
        //! 線速度を置く。body が無ければ何もしない
        void SetVelocity(const NS::Core::Vector3& velocity);
        //! 角速度 (rad/s)。body が無ければ 0
        [[nodiscard]] NS::Core::Vector3 AngularVelocity() const;
        //! 角速度を置く。body が無ければ何もしない
        void SetAngularVelocity(const NS::Core::Vector3& angularVelocity);

        //! 次の物理の 1 歩の間、重心へ力 (N) を掛け続ける。キネマティックと body が無い時は効かない
        void AddForce(const NS::Core::Vector3& force);
        //! 重心へ力積 (N·s) を与え、速度を即座に変える。キネマティックと body が無い時は効かない
        void AddImpulse(const NS::Core::Vector3& impulse);
        //! 次の物理の 1 歩の間、トルク (N·m) を掛け続ける。キネマティックと body が無い時は効かない
        void AddTorque(const NS::Core::Vector3& torque);
        //! 角力積 (N·m·s) を与え、角速度を即座に変える。キネマティックと body が無い時は効かない
        void AddAngularImpulse(const NS::Core::Vector3& angularImpulse);

        //! 眠っている場合 true、起きているか body が無い場合は false
        [[nodiscard]] bool IsSleeping() const;
        //! 眠っている body を起こす
        void WakeUp();

        //! 直近の物理の 1 歩で触れた相手と法線。前の 1 歩の分は残らない
        [[nodiscard]] std::vector<NS::Phys::BodyContact> Contacts() const;

        //! @brief 持ち主を世界の position・rotation へ瞬間移動させ、body も同じ所へ置く。速度はそのまま
        //! @details 親がいる持ち主は、親から見た値へ直して Transform に書く
        void Teleport(const NS::Core::Vector3& position, const NS::Core::Quaternion& rotation);

        NS_REFLECT_BEGIN(RigidBody, Component)
        NS_REFLECT_FIELD(m_kinematic, "キネマティック")
        NS_REFLECT_FIELD(m_mass, "質量")
        NS_REFLECT_FIELD(m_useGravity, "重力を使う")
        NS_REFLECT_FIELD(m_gravity, "重力")
        NS_REFLECT_FIELD(m_friction, "摩擦")
        NS_REFLECT_FIELD(m_restitution, "跳ね返り")
        NS_REFLECT_FIELD(m_linearDamping, "移動の減衰")
        NS_REFLECT_FIELD(m_angularDamping, "回転の減衰")
        NS_REFLECT_FIELD(m_continuousCollision, "連続衝突判定")
        NS_REFLECT_FIELD(m_lockPositionX, "X 移動を固定")
        NS_REFLECT_FIELD(m_lockPositionY, "Y 移動を固定")
        NS_REFLECT_FIELD(m_lockPositionZ, "Z 移動を固定")
        NS_REFLECT_FIELD(m_lockRotationX, "X 回転を固定")
        NS_REFLECT_FIELD(m_lockRotationY, "Y 回転を固定")
        NS_REFLECT_FIELD(m_lockRotationZ, "Z 回転を固定")
        NS_REFLECT_END()

    private:
        // 持ち主の Scene の PhysicsScene。Scene に居なければ null。控えを持つと Scene と正が 2 つになるので毎回引く
        [[nodiscard]] NS::Phys::PhysicsScene* ScenePhysics() const noexcept;
        // 受け取るのは持ち主の Scene の PhysicsScene だけ。Collider と同じ理由で、別の PhysicsScene の id を混ぜない
        [[nodiscard]] bool AcceptsScenePhysics(const NS::Phys::PhysicsScene& physics) const;
        // 持ち主の世界の位置と回転。拡縮は形へ焼き込むので外す
        void OwnerWorldPose(NS::Core::Vector3& position, NS::Core::Quaternion& rotation) const noexcept;
        // 持ち主の世界の位置と回転を置く。親がいれば親から見た値へ直す。拡縮は変えない
        void SetOwnerWorldPose(const NS::Core::Vector3& position, const NS::Core::Quaternion& rotation) noexcept;
        // 形にならず追従する collider の body を、持ち主の今の姿勢へ置き直す
        void SyncFollowers(NS::Phys::PhysicsScene& physics);
        // 今掛ける重力。使わない時と、エディタの欄に非有限値が入った時は 0
        [[nodiscard]] NS::Core::Vector3 EffectiveGravity() const noexcept;

        bool m_kinematic = false;            // 力を受けず Transform の姿勢へ運ばれるか
        float m_mass = 1.0f;                 // 質量 (kg)
        bool m_useGravity = true;                                   // 重力を受けるか
        NS::Core::Vector3 m_gravity = NS::Phys::DefaultGravity();   // この物体の重力の加速度 (m/s^2)
        float m_friction = 0.2f;             // 摩擦
        float m_restitution = 0.0f;          // 跳ね返り
        float m_linearDamping = 0.05f;       // 移動の減衰
        float m_angularDamping = 0.05f;      // 回転の減衰
        bool m_continuousCollision = false;  // 1 歩で動いた道を掃引して当てるか
        bool m_lockPositionX = false;        // 世界の X に沿った移動を止める
        bool m_lockPositionY = false;        // 世界の Y に沿った移動を止める
        bool m_lockPositionZ = false;        // 世界の Z に沿った移動を止める
        bool m_lockRotationX = false;        // 世界の X まわりの回転を止める
        bool m_lockRotationY = false;        // 世界の Y まわりの回転を止める
        bool m_lockRotationZ = false;        // 世界の Z まわりの回転を止める

        JPH::BodyID m_bodyId;                     // 動く body。作っていなければ無効
        NS::Phys::BodyMotion m_appliedMotion;     // 最後に body へ入れた動き方。欄の変化を見つけるのに使う
        NS::Core::Vector3 m_appliedGravity;       // 最後に掛けた重力。変わった時に眠った body を起こすのに使う
        NS::Core::Matrix m_followedWorld;         // 追従する collider を最後に置き直した時の持ち主の世界行列
    };
} // namespace NS::Obj
