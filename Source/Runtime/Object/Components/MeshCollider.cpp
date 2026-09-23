#include "Runtime/Object/Components/MeshCollider.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/MeshCollision.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace NS::Obj
{
    namespace
    {
        // 分解と組み直しの丸め誤差 (1e-6 前後) より 2 桁大きく取った、拡縮 1 あたりの許容差
        constexpr float k_ShearTolerance = 1.0e-4f;

        // 分解した拡縮と回転から 3x3 を組み直し、元と合わなければ歪みがある
        // 歪みは縦横で違う拡縮の親の下に回転した子を置いた時に出て、位置・回転・拡縮の 3 つでは表せない
        [[nodiscard]] bool HasShear(const NS::Core::Matrix& world, const NS::Core::AffineDecomposition& parts) noexcept
        {
            const NS::Core::Matrix rebuilt =
                NS::Core::Matrix::CreateScale(parts.scale) * NS::Core::Matrix::CreateFromQuaternion(parts.rotation);
            const float tolerance = k_ShearTolerance * std::max({1.0f, parts.scale.x, parts.scale.y, parts.scale.z});
            for (int row = 0; row < 3; ++row)
            {
                for (int column = 0; column < 3; ++column)
                {
                    if (std::abs(rebuilt.m[row][column] - world.m[row][column]) > tolerance)
                    {
                        return true;
                    }
                }
            }
            return false;
        }
    } // namespace

    MeshCollider::MeshCollider() noexcept {}

    void MeshCollider::SetCollision(const NS::Phys::MeshCollision* collision) noexcept
    {
        m_collision = collision;
    }

    const NS::Phys::MeshCollision* MeshCollider::Collision() const noexcept
    {
        return m_collision;
    }

    std::vector<NS::Phys::Triangle> MeshCollider::WorldTriangles() const
    {
        if (m_collision == nullptr)
        {
            return {};
        }
        const GameObject* owner = Owner();
        if (owner == nullptr)
        {
            return m_collision->triangles;
        }

        const NS::Core::Matrix world = owner->Root().WorldMatrix();
        std::vector<NS::Phys::Triangle> result;
        result.reserve(m_collision->triangles.size());
        for (const NS::Phys::Triangle& tri : m_collision->triangles)
        {
            result.push_back(NS::Phys::Triangle{NS::Core::Vector3::Transform(tri.v0, world),
                                                NS::Core::Vector3::Transform(tri.v1, world),
                                                NS::Core::Vector3::Transform(tri.v2, world)});
        }
        return result;
    }

    JPH::BodyID MeshCollider::SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        if (m_collision == nullptr)
        {
            return JPH::BodyID{};
        }

        NS::Core::Matrix world = NS::Core::Matrix::Identity;
        if (const GameObject* owner = Owner())
        {
            world = owner->Root().WorldMatrix();
        }
        const NS::Core::AffineDecomposition parts = NS::Core::DecomposeAffine(world);
        // 描画は 4x4 の行列で歪みまで出すので、形の共有をやめて世界座標の三角形から作り、描画と当たりを揃える
        if (HasShear(world, parts))
        {
            return physics.SyncMesh(current, WorldTriangles(), NS::Phys::ObjectLayers::Terrain);
        }

        const NS::Phys::MeshCollision& shared = *m_collision;
        return physics.SyncMeshShape(
            current, shared, parts.translation, parts.rotation, parts.scale, NS::Phys::ObjectLayers::Terrain);
    }

    void MeshCollider::ResolveAssets(AssetManager& assets)
    {
        const GameObject* owner = Owner();
        if (owner == nullptr)
        {
            return;
        }

        const MeshRenderer* renderer = owner->FindComponent<MeshRenderer>();
        if (renderer == nullptr)
        {
            NS_LOG_WARN(Scene, "MeshCollider: 同じ object に MeshRenderer が無く、 当たりは空のまま");
            return;
        }

        const NS::Phys::MeshCollision* collision = assets.GetOrLoadMeshCollision(renderer->MeshRef());
        if (collision == nullptr)
        {
            collision = assets.GetOrLoadMeshCollision("cube");
        }

        SetCollision(collision);
    }

    // data からは当たり無しで作る
    NS_CLASS(MeshCollider)
} // namespace NS::Obj
