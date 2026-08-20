#include "Game/Level/LaunchedBodyComponent.h"

#include "Runtime/Core/Clock.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"
#include "Runtime/Object/Components/ShadowComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>

// TODO: 真下の地面しか見ていない。飛んだ物が壁を抜けるのが気になったら掃引へ替える
namespace NS::Game::Level
{
    namespace
    {
        // 当たり箱を持たない配置物の半分の高さ
        constexpr float k_DefaultHalfHeight = 0.5f;
        // 真下を探す上限。これより下に何も無ければ落ち続ける
        constexpr float k_GroundProbeDistance = 64.0f;
    } // namespace

    void LaunchedBodyComponent::Launch(const NS::Core::Vector3& velocity)
    {
        // 非有限値は位置へ流れ、配置物が二度と描かれない場所へ飛ぶ
        if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
            return;

        m_velocity = velocity;
        m_flying = true;
        m_grounded = false;
        m_restAge = 0.0f;
        SetColliderActive(false);
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
        const float halfY = HalfHeight();
        m_grounded = false;
        if (NS::Object::Scene* scene = Owner()->OwningScene())
        {
            float dist = 0.0f;
            const bool found =
                NS::Object::ShadowComponent::GroundBelow(next, scene->Physics().Aabbs(), k_GroundProbeDistance, dist);
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
        auto* box = Owner()->FindComponent<NS::Object::BoxColliderComponent>();
        if (box == nullptr || box->IsActiveSelf() == active)
            return;

        box->SetActive(active);
        if (NS::Object::Scene* scene = Owner()->OwningScene())
            scene->SyncPhysics();
    }

    float LaunchedBodyComponent::HalfHeight() const noexcept
    {
        if (Owner() == nullptr)
            return k_DefaultHalfHeight;
        const auto* box = Owner()->FindComponent<NS::Object::BoxColliderComponent>();
        if (box == nullptr)
            return k_DefaultHalfHeight;
        // GroundBelow が見るのと同じ world 空間の箱から取る。 HalfExtents だと拡縮した配置物が床へ潜る
        return box->WorldAABB().Extents.y;
    }

    NS_CLASS(LaunchedBodyComponent)
} // namespace NS::Game::Level
