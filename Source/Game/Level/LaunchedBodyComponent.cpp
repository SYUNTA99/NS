#include "Game/Level/LaunchedBodyComponent.h"

#include "Game/Level/BreakableComponent.h"
#include "Game/Level/ColliderBounds.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Clock.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Object/Components/ColliderComponent.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace NS::Game::Level
{
    namespace
    {
        // 当たり箱を持たない配置物の、各軸の半分の大きさ
        constexpr float k_DefaultHalfExtent = 0.5f;

        // 壁とみなす法線の上限。床は着地で必ず当たるので分けないと着地で割れる。cos 45 に合わせて 0.7
        constexpr float k_WallNormalY = 0.7f;

        constexpr float k_MinSpinSpeed = 1.0e-4f;

        constexpr float k_DebrisScale = 0.25f;
        constexpr NS::Core::Vector3 k_DebrisBaseColor{0.35f, 0.32f, 0.30f};

        [[nodiscard]] bool IsFinite(const NS::Core::Vector3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    } // namespace

    LaunchedBodyComponent::LaunchedBodyComponent() noexcept
        : NS::Object::Component(NS::Object::TickPriority::LateUpdate)
    {}

    NS::Core::Vector3 LaunchedBodyComponent::TumbleFrom(const NS::Core::Vector3& velocity) const noexcept
    {
        const float horizontal = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        const float rate = m_spinPerSpeed * horizontal;
        // 下限に届かない水平の速さで軸を正規化すると 0 除算になり、姿勢へ NaN が流れる
        if (!std::isfinite(rate) || horizontal < k_MinSpinSpeed)
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};

        // 軸は上向きと進む向きの外積。正の角度で上面が進行方向へ倒れる前転になる
        const NS::Core::Vector3 axis{velocity.z / horizontal, 0.0f, -velocity.x / horizontal};
        return axis * rate;
    }

    NS::Core::Vector3 LaunchedBodyComponent::Velocity() const noexcept
    {
        if (!m_flying || m_physics == nullptr)
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        return m_physics->BodyVelocity(m_bodyId);
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

    JPH::BodyID LaunchedBodyComponent::CreateFlyingBody(NS::Physics::PhysicsWorld& physics) const
    {
        NS::Core::AABB bounds{};
        if (!TryGetColliderBounds(*Owner(), bounds))
        {
            // 当たりの無い破片は描画スケールから形を作る。既定の大きさだと小さい破片が浮いて止まる
            const NS::Core::Vector3 scale = RootTransform().Scale();
            bounds.Center = RootTransform().Position();
            bounds.Extents = NS::Core::Vector3{k_DefaultHalfExtent * std::abs(scale.x),
                                               k_DefaultHalfExtent * std::abs(scale.y),
                                               k_DefaultHalfExtent * std::abs(scale.z)};
        }

        NS::Physics::DynamicBodyDesc desc;
        // 破片同士は当たらないレイヤーに置く。散った破片が互いを押し合うと元の勢いが読めなくなる
        desc.layer = Owner()->IsTransient() ? NS::Physics::ObjectLayers::Debris : NS::Physics::ObjectLayers::Rock;
        desc.restitution = m_restitution;
        desc.friction = m_friction;
        if (const auto* breakable = Owner()->FindComponent<BreakableComponent>())
            desc.mass = breakable->Mass();

        // TODO: どの形も外接箱で近似している。球の的が箱として転がるのが気になったら形ごとに分ける
        NS::Core::OBB box;
        box.center = bounds.Center;
        box.halfExtentX = std::max(bounds.Extents.x, 1.0e-3f);
        box.halfExtentY = std::max(bounds.Extents.y, 1.0e-3f);
        box.halfExtentZ = std::max(bounds.Extents.z, 1.0e-3f);
        return physics.AddDynamicBox(box, desc);
    }

    void LaunchedBodyComponent::RemoveFlyingBody() noexcept
    {
        if (m_physics == nullptr)
            return;

        m_physics->RemoveBody(m_bodyId);
        m_bodyId = JPH::BodyID{};
    }

    void LaunchedBodyComponent::Launch(const NS::Core::Vector3& velocity)
    {
        // 非有限値は位置へ流れ、配置物が二度と描かれない場所へ飛ぶ
        if (!IsFinite(velocity) || Owner() == nullptr)
            return;
        NS::Object::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;

        if (!m_flying)
        {
            // 置かれた当たりを先に外す。残すと同じ場所に静的と動的の body が二重に立つ
            SetColliderActive(false);
            m_physics = &scene->Physics();
            m_bodyId = CreateFlyingBody(*m_physics);
            if (m_bodyId.IsInvalid())
            {
                SetColliderActive(true);
                return;
            }
            m_flying = true;
        }

        m_physics->SetBodyVelocity(m_bodyId, velocity);
        m_physics->SetBodyAngularVelocity(m_bodyId, TumbleFrom(velocity));
        m_restAge = 0.0f;
    }

    void LaunchedBodyComponent::ComeToRest()
    {
        RemoveFlyingBody();
        m_flying = false;
        m_restAge = 0.0f;
        SetColliderActive(true);
    }

    void LaunchedBodyComponent::HideAndSleep()
    {
        if (auto* mesh = Owner()->FindComponent<NS::Object::MeshRendererComponent>())
            mesh->SetActive(false);
        SetColliderActive(false);
        SetActive(false);
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

        RemoveFlyingBody();
        m_flying = false;

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
            auto* body = spawned->FindComponent<LaunchedBodyComponent>();
            if (body == nullptr)
                continue;
            // 破片だけ寿命を持つ。壊すたびに増えるので、止まったら消さないと世界に積み上がり続ける
            body->SetRestLifeSeconds(m_debrisLifeSeconds);
            // 破片からは破片を出さない。0 にしないと破片が壁へ当たるたびに破片を撒く
            body->SetDebrisCount(0);
            body->Launch(NS::Core::Vector3{std::cos(angle) * speed, up * speed, std::sin(angle) * speed});
        }

        HideAndSleep();
    }

    void LaunchedBodyComponent::OnUpdate()
    {
        const float dt = NS::Core::FrameTimer::FixedDelta();
        if (!m_flying)
        {
            // 0 は消えない指定。押し飛ばしただけの配置物は場に残す
            if (m_restLifeSeconds <= 0.0f)
                return;
            m_restAge += dt;
            if (m_restAge < m_restLifeSeconds)
                return;
            HideAndSleep();
            return;
        }

        if (m_physics == nullptr)
            return;

        for (const NS::Physics::BodyContact& contact : m_physics->ContactsOf(m_bodyId))
        {
            if (contact.normal.y < k_WallNormalY)
            {
                Shatter();
                return;
            }
        }

        RootTransform().SetPosition(m_physics->BodyPosition(m_bodyId));
        RootTransform().SetRotation(m_physics->BodyRotation(m_bodyId));

        // 止まったかを決めるのは Jolt の睡眠。速度のしきい値を自分で持つと 2 か所で止まりを判断することになる
        if (!m_physics->IsBodyAwake(m_bodyId))
            ComeToRest();
    }

    void LaunchedBodyComponent::OnEndPlay()
    {
        RemoveFlyingBody();
        m_flying = false;
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
        {
            if (active)
                collider->SyncToPhysics(scene->Physics());
            else
                collider->RemoveFromPhysics();
        }
    }

    NS_CLASS(LaunchedBodyComponent)
} // namespace NS::Game::Level
