#include "NSlib/CoreObject/SubObjects/MeshCollision.h"

#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
#include "NSlib/Phys/MeshShape.h"
#include "NSlib/Phys/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace NS
{
    namespace
    {
        // 分解と組み直しの丸め誤差 (1e-6 前後) より 2 桁大きく取った、拡縮 1 あたりの許容差
        constexpr float k_ShearTolerance = 1.0e-4f;

        // 分解した拡縮と回転から 3x3 を組み直し、元と合わなければ歪みがある
        // 歪みは縦横で違う拡縮の親の下に回転した子を置いた時に出て、位置・回転・拡縮の 3 つでは表せない
        [[nodiscard]] bool HasShear(const Matrix& world, const AffineDecomposition& parts) noexcept
        {
            const Matrix rebuilt =
                Matrix::CreateScale(parts.scale) * Matrix::CreateFromQuaternion(parts.rotation);
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

    void MeshCollision::SetShape(const Phys::MeshShape* shape) noexcept
    {
        m_shape = shape;
    }

    const Phys::MeshShape* MeshCollision::Shape() const noexcept
    {
        return m_shape;
    }

    std::vector<Phys::Triangle> MeshCollision::WorldTriangles() const
    {
        if (m_shape == nullptr)
        {
            return {};
        }
        if (m_owner == nullptr)
        {
            return m_shape->triangles;
        }

        const Matrix world = m_owner->Root().WorldMatrix();
        std::vector<Phys::Triangle> result;
        result.reserve(m_shape->triangles.size());
        for (const Phys::Triangle& tri : m_shape->triangles)
        {
            result.push_back(Phys::Triangle{Vector3::Transform(tri.v0, world),
                                                Vector3::Transform(tri.v1, world),
                                                Vector3::Transform(tri.v2, world)});
        }
        return result;
    }

    JPH::BodyID MeshCollision::SyncBody(Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        if (m_shape == nullptr)
        {
            return JPH::BodyID{};
        }

        Matrix world = Matrix::Identity;
        if (m_owner != nullptr)
        {
            world = m_owner->Root().WorldMatrix();
        }
        const AffineDecomposition parts = DecomposeAffine(world);
        // 描画は 4x4 の行列で歪みまで出すので、形の共有をやめて世界座標の三角形から作り、描画と当たりを揃える
        if (HasShear(world, parts))
        {
            return physics.SyncMesh(current, WorldTriangles(), Phys::ObjectLayers::Terrain);
        }

        const Phys::MeshShape& shared = *m_shape;
        return physics.SyncMeshShape(
            current, shared, parts.translation, parts.rotation, parts.scale, Phys::ObjectLayers::Terrain);
    }
} // namespace NS
