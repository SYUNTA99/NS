#include "NSlib/Object/Components/MeshCollision.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/AssetManager.h"
#include "NSlib/Object/Components/Model.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Transform.h"
#include "NSlib/Physics/MeshShape.h"
#include "NSlib/Physics/PhysicsScene.h"

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
        [[nodiscard]] bool HasShear(const NS::Matrix& world, const NS::AffineDecomposition& parts) noexcept
        {
            const NS::Matrix rebuilt =
                NS::Matrix::CreateScale(parts.scale) * NS::Matrix::CreateFromQuaternion(parts.rotation);
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

    MeshCollision::MeshCollision() noexcept {}

    void MeshCollision::SetShape(const NS::Phys::MeshShape* shape) noexcept
    {
        m_shape = shape;
    }

    const NS::Phys::MeshShape* MeshCollision::Shape() const noexcept
    {
        return m_shape;
    }

    std::vector<NS::Phys::Triangle> MeshCollision::WorldTriangles() const
    {
        if (m_shape == nullptr)
        {
            return {};
        }
        const Actor* owner = Owner();
        if (owner == nullptr)
        {
            return m_shape->triangles;
        }

        const NS::Matrix world = owner->Root().WorldMatrix();
        std::vector<NS::Phys::Triangle> result;
        result.reserve(m_shape->triangles.size());
        for (const NS::Phys::Triangle& tri : m_shape->triangles)
        {
            result.push_back(NS::Phys::Triangle{NS::Vector3::Transform(tri.v0, world),
                                                NS::Vector3::Transform(tri.v1, world),
                                                NS::Vector3::Transform(tri.v2, world)});
        }
        return result;
    }

    JPH::BodyID MeshCollision::SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        if (m_shape == nullptr)
        {
            return JPH::BodyID{};
        }

        NS::Matrix world = NS::Matrix::Identity;
        if (const Actor* owner = Owner())
        {
            world = owner->Root().WorldMatrix();
        }
        const NS::AffineDecomposition parts = NS::DecomposeAffine(world);
        // 描画は 4x4 の行列で歪みまで出すので、形の共有をやめて世界座標の三角形から作り、描画と当たりを揃える
        if (HasShear(world, parts))
        {
            return physics.SyncMesh(current, WorldTriangles(), NS::Phys::ObjectLayers::Terrain);
        }

        const NS::Phys::MeshShape& shared = *m_shape;
        return physics.SyncMeshShape(
            current, shared, parts.translation, parts.rotation, parts.scale, NS::Phys::ObjectLayers::Terrain);
    }

    void MeshCollision::ResolveAssets(AssetManager& assets)
    {
        const Actor* owner = Owner();
        if (owner == nullptr)
        {
            return;
        }

        const Model* renderer = owner->ModelPart();
        if (renderer == nullptr)
        {
            NS_LOG_WARN(Scene, "MeshCollision: 同じ object に MeshRenderer が無く、 当たりは空のまま");
            return;
        }

        const NS::Phys::MeshShape* shape = assets.GetOrLoadMeshShape(renderer->MeshRef());
        if (shape == nullptr)
        {
            shape = assets.GetOrLoadMeshShape("cube");
        }

        SetShape(shape);
    }

    // data からは当たり無しで作る
    NS_CLASS(MeshCollision)
} // namespace NS::Obj
