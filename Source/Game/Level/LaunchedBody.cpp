#include "Game/Level/LaunchedBody.h"

#include "Game/Level/Breakable.h"
#include "Runtime/Platform/Clock.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/JoltCharacter.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace NS::Game::Level
{
    namespace
    {
        [[nodiscard]] bool IsFinite(const NS::Core::Vector3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    } // namespace

    LaunchedBody::LaunchedBody() noexcept
        : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate)
    {}

    NS::Core::Vector3 LaunchedBody::TumbleFrom(const NS::Core::Vector3& velocity) const noexcept
    {
        NS::Core::Vector3 forward{};
        if (!NS::Core::TryNormalizeHorizontal(velocity, forward))
        {
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        }
        const float horizontal = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        const float rate = m_spinPerSpeed * horizontal;
        if (!std::isfinite(rate))
        {
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        }

        // 軸は上向きと進む向きの外積。正の角度で上面が進行方向へ倒れる前転になる
        const NS::Core::Vector3 axis{forward.z, 0.0f, -forward.x};
        return axis * rate;
    }

    NS::Core::Vector3 LaunchedBody::Velocity() const
    {
        if (!m_flying || Owner() == nullptr)
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        const NS::Obj::RigidBody* rigidBody = Owner()->FindComponent<NS::Obj::RigidBody>();
        if (rigidBody == nullptr)
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        return rigidBody->Velocity();
    }

    void LaunchedBody::SetRestLifeSeconds(float seconds) noexcept
    {
        if (!std::isfinite(seconds) || seconds < 0.0f)
            return;
        m_restLifeSeconds = seconds;
    }

    void LaunchedBody::SetDebrisCount(int count) noexcept
    {
        m_debrisCount = std::max(0, count);
    }

    NS::Obj::RigidBody* LaunchedBody::EnsureRigidBody()
    {
        if (Owner() == nullptr)
            return nullptr;
        if (NS::Obj::RigidBody* found = Owner()->FindComponent<NS::Obj::RigidBody>())
            return found;
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return nullptr;

        // 積み忘れた配置物でも飛ばせるよう、置かれた姿のままのキネマティックで足す
        NS::Obj::RigidBody* added = Owner()->AddComponent<NS::Obj::RigidBody>();
        added->SetKinematic(true);
        // collider に自分の静的な body を外させてから形を集める。残すと同じ場所に静的と動く body が二重に立つ
        for (NS::Obj::Component* comp : Owner()->Components())
        {
            NS::Obj::Collider* collider = NS::Obj::ComponentCast<NS::Obj::Collider>(comp);
            if (collider != nullptr && collider->IsActive())
                collider->SyncToPhysics(scene->Physics());
        }
        added->SyncToPhysics(scene->Physics());
        return added;
    }

    void LaunchedBody::Launch(const NS::Core::Vector3& velocity)
    {
        // 非有限値は位置へ流れ、配置物が二度と描かれない場所へ飛ぶ
        if (!IsFinite(velocity))
            return;
        NS::Obj::RigidBody* rigidBody = EnsureRigidBody();
        if (rigidBody == nullptr || rigidBody->BodyId().IsInvalid())
            return;

        // 速度はダイナミックへ切り替えてから置く。次の 1 歩を待つとキネマティックの運びが速度を上書きする
        rigidBody->SetKinematic(false);
        rigidBody->RefreshMotion();
        rigidBody->SetVelocity(velocity);
        rigidBody->SetAngularVelocity(TumbleFrom(velocity));
        m_flying = true;
        m_restAge = 0.0f;
    }

    void LaunchedBody::ComeToRest()
    {
        m_flying = false;
        m_restAge = 0.0f;
        NS::Obj::RigidBody* rigidBody = Owner()->FindComponent<NS::Obj::RigidBody>();
        if (rigidBody == nullptr)
            return;
        // 残った速度はキネマティックの運びに混ざる。止まった所へ置いたまま動かさない
        rigidBody->SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        rigidBody->SetAngularVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        rigidBody->SetKinematic(true);
        rigidBody->RefreshMotion();
    }

    void LaunchedBody::HideAndSleep()
    {
        m_flying = false;
        if (NS::Obj::MeshRenderer* mesh = Owner()->FindComponent<NS::Obj::MeshRenderer>())
            mesh->SetActive(false);

        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (NS::Obj::RigidBody* rigidBody = Owner()->FindComponent<NS::Obj::RigidBody>())
        {
            if (scene != nullptr)
                rigidBody->RemoveFromPhysics(scene->Physics());
            rigidBody->SetActive(false);
        }
        // RigidBody を止めると collider が自分の body を持ち直せる。止めて外し、当たりを残さない
        for (NS::Obj::Component* comp : Owner()->Components())
        {
            NS::Obj::Collider* collider = NS::Obj::ComponentCast<NS::Obj::Collider>(comp);
            if (collider == nullptr)
                continue;
            collider->SetActive(false);
            if (scene != nullptr)
                collider->RemoveFromPhysics(scene->Physics());
        }
        SetActive(false);
    }

    void LaunchedBody::Shatter()
    {
        if (Owner() == nullptr)
            return;
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;

        const NS::Core::Vector3 origin = RootTransform().Position();
        const NS::Obj::RigidBody* source = Owner()->FindComponent<NS::Obj::RigidBody>();
        float mass = 1.0f;
        if (source != nullptr)
        {
            mass = source->EffectiveMass();
        }
        Breakable* breakable = Owner()->FindComponent<Breakable>();
        if (breakable != nullptr)
        {
            breakable->SetActive(false);
        }
        // 破片より先に自分の当たりを外す。残すと同じ場所に湧いた破片が押し出されて勢いが読めなくなる
        HideAndSleep();

        // 重い物ほど破片が飛ばない。押し飛ばしと同じ向きの質量感を破片でも見せる
        const float speed = m_debrisSpeed / mass;
        for (int i = 0; i < m_debrisCount; ++i)
        {
            const float angle = 2.0f * NS::Core::k_Pi * static_cast<float>(i) / static_cast<float>(m_debrisCount);
            // 浮きは交互に変える。全部同じ高さだと 1 つの輪に見えて壊れた量が伝わらない
            const float up = 0.5f + 0.5f * static_cast<float>(i % 2);
            std::unique_ptr<NS::Obj::GameObject> owned = std::make_unique<NS::Obj::GameObject>();
            owned->Root().SetPosition(origin);
            owned->Root().SetScale(NS::Core::Vector3{m_debrisScale, m_debrisScale, m_debrisScale});
            NS::Obj::MeshRenderer* mesh = owned->AddComponent<NS::Obj::MeshRenderer>();
            mesh->SetMeshRef("cube");
            mesh->SetMaterialRef("player");
            mesh->SetBaseColor(m_debrisBaseColor);
            owned->AddComponent<NS::Obj::BoxCollider>();
            NS::Obj::RigidBody* debrisBody = owned->AddComponent<NS::Obj::RigidBody>();
            // 破片同士は当たらない種別に置く。散った破片が互いを押し合うと元の勢いが読めなくなる
            debrisBody->SetObjectLayer(NS::Phys::ObjectLayers::Debris);
            // 面の手触りは壊れた物と揃える
            if (source != nullptr)
            {
                debrisBody->SetFriction(source->Friction());
                debrisBody->SetRestitution(source->Restitution());
            }
            owned->AddComponent<LaunchedBody>();
            NS::Obj::GameObject* spawned = scene->SpawnTransient(std::move(owned));
            if (spawned == nullptr)
                continue;
            LaunchedBody* body = spawned->FindComponent<LaunchedBody>();
            if (body == nullptr)
                continue;
            // 破片だけ寿命を持つ。壊すたびに増えるので、止まったら消さないと世界に積み上がり続ける
            body->SetRestLifeSeconds(m_debrisLifeSeconds);
            // 破片からは破片を出さない。0 にしないと破片が壁へ当たるたびに破片を撒く
            body->SetDebrisCount(0);
            body->Launch(NS::Core::Vector3{std::cos(angle) * speed, up * speed, std::sin(angle) * speed});
        }

    }

    void LaunchedBody::OnUpdate()
    {
        const float dt = NS::Platform::FrameTimer::FixedDelta();
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

        // 姿勢の書き戻しは RigidBody が物理の直後に済ませている。ここは接触と眠りだけを見る
        const NS::Obj::RigidBody* rigidBody = Owner()->FindComponent<NS::Obj::RigidBody>();
        if (rigidBody == nullptr)
        {
            m_flying = false;
            return;
        }

        for (const NS::Phys::BodyContact& contact : rigidBody->Contacts())
        {
            // 歩ける面は床。着地で必ず当たる床を分けないと着地で割れる
            if (!NS::Phys::IsWalkableNormal(contact.normal.y))
            {
                Shatter();
                return;
            }
        }

        // 止まったかを決めるのは Jolt の睡眠。速度のしきい値を自分で持つと 2 か所で止まりを判断することになる
        if (rigidBody->IsSleeping())
            ComeToRest();
    }

    void LaunchedBody::OnEndPlay()
    {
        // body は RigidBody が自分で外す
        m_flying = false;
    }

    NS_CLASS(LaunchedBody)
} // namespace NS::Game::Level
