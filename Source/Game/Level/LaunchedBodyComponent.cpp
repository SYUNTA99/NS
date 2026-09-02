#include "Game/Level/LaunchedBodyComponent.h"

#include "Game/Level/BreakableComponent.h"
#include "Game/Level/ColliderBounds.h"
#include "Runtime/Core/Clock.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/ColliderComponent.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/Capsule.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace NS::Game::Level
{
    namespace
    {
        // 当たり箱を持たない配置物の半分の高さ
        constexpr float k_DefaultHalfHeight = 0.5f;
        // 真下を探す上限。これより下に何も無ければ落ち続ける
        constexpr float k_GroundProbeDistance = 64.0f;

        // 回る向きが決まる水平の速さの下限。これ未満は軸の正規化が 0 除算になり、姿勢へ NaN が流れる
        constexpr float k_MinSpinSpeed = 1.0e-4f;

        // 壁とみなす法線の上限。床は毎歩当たるので分けないと着地で割れる。cos 45 ≈ 0.707 に合わせて 0.7
        constexpr float k_WallNormalY = 0.7f;

        // 床へぴったり乗せた球は誤差でめり込み、掃引が 1 歩目から当たる。半径をこの分だけ細くして逃がす
        constexpr float k_SweepSkin = 0.01f;

        constexpr float k_DebrisScale = 0.25f;
        constexpr NS::Core::Vector3 k_DebrisBaseColor{0.35f, 0.32f, 0.30f};
    } // namespace

    void LaunchedBodyComponent::Launch(const NS::Core::Vector3& velocity)
    {
        // 非有限値は位置へ流れ、配置物が二度と描かれない場所へ飛ぶ
        if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
            return;

        // 控えるのは回っていない飛び始めだけ。回した後の当たり箱は高さが伸びて床の上へ乗らない
        if (!m_flying)
        {
            m_spinHome = RootTransform().Rotation();
            m_halfHeight = HalfHeight();
            m_spinAngle = 0.0f;
        }

        m_velocity = velocity;
        m_flying = true;
        m_grounded = false;
        m_restAge = 0.0f;
        BeginSpin(velocity);
        SetColliderActive(false);
    }

    void LaunchedBodyComponent::BeginSpin(const NS::Core::Vector3& velocity) noexcept
    {
        const float horizontal = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        m_spinRate = m_spinPerSpeed * horizontal;
        if (!std::isfinite(m_spinRate) || horizontal < k_MinSpinSpeed)
        {
            m_spinRate = 0.0f;
            return;
        }
        // 上向きと進む向きの外積。正の角度で上面が進行方向へ倒れる前転になる
        m_spinAxis = NS::Core::Vector3{velocity.z / horizontal, 0.0f, -velocity.x / horizontal};
    }

    void LaunchedBodyComponent::UpdateSpin(float dt)
    {
        if (m_grounded)
        {
            // 当たり箱は回らない。傾いたまま滑って止まると絵と当たりがずれる
            if (m_spinAngle != 0.0f)
            {
                m_spinAngle = 0.0f;
                RootTransform().SetRotation(m_spinHome);
            }
            return;
        }

        m_spinAngle += m_spinRate * dt;
        RootTransform().SetRotation(NS::Core::Quaternion::Concatenate(
            m_spinHome, NS::Core::Quaternion::CreateFromAxisAngle(m_spinAxis, m_spinAngle)));
    }

    void LaunchedBodyComponent::SetRestLifeSeconds(float seconds) noexcept
    {
        if (!std::isfinite(seconds) || seconds < 0.0f)
            return;
        m_restLifeSeconds = seconds;
    }

    void LaunchedBodyComponent::SetDebrisCount(int count) noexcept
    {
        m_debrisCount = std::max(0, count);
    }

    void LaunchedBodyComponent::Shatter()
    {
        if (Owner() == nullptr)
            return;
        NS::Object::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;

        const NS::Core::Vector3 origin = RootTransform().Position();
        float mass = 1.0f;
        if (auto* breakable = Owner()->FindComponent<BreakableComponent>())
            mass = breakable->Mass();
        if (!std::isfinite(mass) || mass < 0.01f)
            mass = 0.01f;

        // 重い物ほど破片が飛ばない。押し飛ばしと同じ向きの質量感を破片でも見せる
        const float speed = m_debrisSpeed / mass;
        for (int i = 0; i < m_debrisCount; ++i)
        {
            const float angle = 2.0f * NS::Core::k_Pi * static_cast<float>(i) / static_cast<float>(m_debrisCount);
            // 浮きは交互に変える。全部同じ高さだと 1 つの輪に見えて壊れた量が伝わらない
            const float up = 0.5f + 0.5f * static_cast<float>(i % 2);
            auto owned = std::make_unique<NS::Object::GameObject>();
            owned->Root().SetPosition(origin);
            owned->Root().SetScale(NS::Core::Vector3{k_DebrisScale, k_DebrisScale, k_DebrisScale});
            auto* mesh = owned->AddComponent<NS::Object::MeshRendererComponent>();
            mesh->SetMeshRef("cube");
            mesh->SetMaterialRef("player");
            mesh->SetBaseColor(k_DebrisBaseColor);
            owned->AddComponent<LaunchedBodyComponent>();
            NS::Object::GameObject* spawned = scene->SpawnTransient(std::move(owned));
            if (spawned == nullptr)
                continue;
            // 所有を渡した後の元のポインタは使わない。戻り値から引き直す
            auto* body = spawned->FindComponent<LaunchedBodyComponent>();
            if (body == nullptr)
                continue;
            // 破片だけ寿命を持つ。壊すたびに増えるので、止まったら消さないと世界に積み上がり続ける
            body->SetRestLifeSeconds(m_debrisLifeSeconds);
            // 破片は割れない。0 にしないと破片が壁へ当たるたびに破片を撒く
            body->SetDebrisCount(0);
            body->Launch(NS::Core::Vector3{std::cos(angle) * speed, up * speed, std::sin(angle) * speed});
        }

        m_flying = false;
        m_velocity = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        // 更新中に消すと集めた並びに解放済みの位置が残る。寝かせるだけにする
        if (auto* mesh = Owner()->FindComponent<NS::Object::MeshRendererComponent>())
            mesh->SetActive(false);
        SetColliderActive(false);
        SetActive(false);
    }

    void LaunchedBodyComponent::OnUpdate()
    {
        const float dt = NS::Core::FrameTimer::FixedDelta();
        if (!m_flying)
        {
            // 0 は消えない指定。押し飛ばした配置物は場に残り、破壊の側と非対称にしない
            if (m_restLifeSeconds <= 0.0f)
                return;
            m_restAge += dt;
            if (m_restAge < m_restLifeSeconds)
                return;
            // 更新の最中に配置物を消すと、集めた並びに解放済みの位置が残る。描画と当たりだけ止める
            if (auto* mesh = Owner()->FindComponent<NS::Object::MeshRendererComponent>())
                mesh->SetActive(false);
            SetColliderActive(false);
            SetActive(false);
            return;
        }

        m_velocity.y += m_gravity * dt;

        const NS::Core::Vector3 position = RootTransform().Position();
        NS::Core::Vector3 next{
            position.x + m_velocity.x * dt, position.y + m_velocity.y * dt, position.z + m_velocity.z * dt};

        // 進んだ先で床を見る。今の位置で見ると 1 固定ステップぶん床へ潜ってから乗る
        const float halfY = m_halfHeight;
        m_grounded = false;
        if (NS::Object::Scene* scene = Owner()->OwningScene())
        {
            NS::Physics::Capsule cap{};
            cap.center = position;
            cap.halfHeight = 0.0f;
            cap.radius = std::max(0.0f, halfY - k_SweepSkin);
            const NS::Physics::SweepHit swept = scene->Physics().SweepCapsule(
                cap, NS::Core::Vector3{m_velocity.x * dt, m_velocity.y * dt, m_velocity.z * dt});
            if (swept.hit && swept.normal.y < k_WallNormalY)
            {
                Shatter();
                return;
            }

            float dist = 0.0f;
            const bool found = scene->Physics().RaycastDown(next, k_GroundProbeDistance, dist);
            if (found && dist <= halfY)
            {
                next.y += halfY - dist;
                m_velocity.y = 0.0f;
                m_grounded = true;
            }
        }

        if (m_grounded)
        {
            // 接地中だけ水平を殺す。空中でも掛けると飛距離が勢いの表示にならない
            const float decay = std::max(0.0f, 1.0f - m_groundFriction * dt);
            m_velocity.x *= decay;
            m_velocity.z *= decay;
        }

        UpdateSpin(dt);
        RootTransform().SetPosition(next);

        const float horizontal = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z);
        if (m_grounded && horizontal < m_restSpeed)
        {
            m_velocity = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
            m_flying = false;
            m_restAge = 0.0f;
            SetColliderActive(true);
        }
    }

    void LaunchedBodyComponent::SetColliderActive(bool active)
    {
        if (Owner() == nullptr)
            return;
        // 当たり箱を持たない配置物でも飛べるようにする
        auto* collider = Owner()->FindComponent<NS::Object::ColliderComponent>();
        if (collider == nullptr || collider->IsActiveSelf() == active)
            return;

        collider->SetActive(active);
        if (NS::Object::Scene* scene = Owner()->OwningScene())
            scene->SyncPhysics();
    }

    float LaunchedBodyComponent::HalfHeight() const noexcept
    {
        if (Owner() == nullptr)
            return k_DefaultHalfHeight;
        NS::Core::AABB bounds{};
        if (!TryGetColliderBounds(*Owner(), bounds))
        {
            // 当たり箱の無い破片は描画スケールから半分の高さを作る。既定値のままだと小さい破片が浮いて止まる
            const float scaleY = Owner()->Root().Scale().y;
            if (scaleY > 0.0f)
                return k_DefaultHalfHeight * scaleY;
            return k_DefaultHalfHeight;
        }
        // RaycastDown が見るのと同じ world 空間の箱から取る。 HalfExtents だと拡縮した配置物が床へ潜る
        return bounds.Extents.y;
    }

    NS_CLASS(LaunchedBodyComponent)
} // namespace NS::Game::Level
