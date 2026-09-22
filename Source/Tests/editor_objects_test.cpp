#include "Editor/EditorObjects.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/GameObject.h"

#include <gtest/gtest.h>

namespace
{
    void ExpectSameBounds(const NS::Core::AABB& actual, const NS::Core::AABB& expected)
    {
        EXPECT_FLOAT_EQ(actual.Center.x, expected.Center.x);
        EXPECT_FLOAT_EQ(actual.Center.y, expected.Center.y);
        EXPECT_FLOAT_EQ(actual.Center.z, expected.Center.z);
        EXPECT_FLOAT_EQ(actual.Extents.x, expected.Extents.x);
        EXPECT_FLOAT_EQ(actual.Extents.y, expected.Extents.y);
        EXPECT_FLOAT_EQ(actual.Extents.z, expected.Extents.z);
    }
} // namespace

// メッシュを描かない配置物と、メッシュがまだ解決されていない配置物は 1m 立方で選ぶ
TEST(EditorPickBounds, ObjectWithoutADrawnMeshIsPickedByTheUnitCell)
{
    const NS::Core::AABB unitCell{NS::Core::Vector3{0.0f, 0.0f, 0.0f}, NS::Core::Vector3{0.5f, 0.5f, 0.5f}};

    NS::Obj::GameObject camera;
    ExpectSameBounds(NS::Editor::PickLocalBounds(camera), unitCell);

    NS::Obj::GameObject unresolved;
    unresolved.AddComponent<NS::Obj::MeshRenderer>();
    ExpectSameBounds(NS::Editor::PickLocalBounds(unresolved), unitCell);
}
