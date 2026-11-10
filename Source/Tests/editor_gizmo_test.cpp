#include "Editor/GizmoEditor.h"
#include "Editor/GridMath.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/CoreObject/Actors/MapPartsActor.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Gfx/D3DStaticMesh.h"
#include "NSlib/Phys/MeshShape.h"
#include "Tests/TestCheckLayer.h"
#include "Tests/TestHiddenApp.h"
#include "TestSpawn.h"

#include <gtest/gtest.h>

#include <optional>
#include <array>
#include <memory>
#include <utility>

namespace
{
    NS::Phys::MeshShape PickSurface(float z, float x = 0.0f)
    {
        NS::Phys::MeshShape surface;
        surface.triangles.push_back({{x - 1.0f, -1.0f, z}, {x + 1.0f, -1.0f, z}, {x, 1.0f, z}});
        return surface;
    }

    NS::Matrix MakeViewProjection(const NS::Editor::ViewRect& view)
    {
        const NS::Matrix viewMatrix =
            NS::Matrix::CreateLookAt(NS::Vector3{3.0f, 3.0f, -5.0f}, NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3::Up);
        const float aspect = static_cast<float>(view.width) / static_cast<float>(view.height);
        return viewMatrix * NS::Matrix::CreatePerspectiveFieldOfView(1.0f, aspect, 0.1f, 100.0f);
    }

    std::optional<NS::Vector2> FindMoveHandle(const NS::Matrix& viewProjection, const NS::Editor::ViewRect& view)
    {
        const NS::Size2D viewport = NS::Editor::ViewRectSize(view);
        for (int y = 0; y < view.height; ++y)
        {
            for (int x = 0; x < view.width; ++x)
            {
                const NS::Vector2 point{static_cast<float>(x), static_cast<float>(y)};
                const NS::Editor::GizmoAxis axis = NS::Editor::GizmoEditor::ToolHandlePick(NS::Vector3::Zero,
                                                                                           NS::Quaternion::Identity,
                                                                                           NS::Editor::GizmoTool::Move,
                                                                                           point,
                                                                                           viewProjection,
                                                                                           viewport);
                if (axis != NS::Editor::GizmoAxis::None)
                {
                    return point;
                }
            }
        }
        return std::nullopt;
    }
} // namespace

// 休ませたギズモは引き続けない。モードを移る間に引きが残ると、組み直した後に消えた物を動かす
TEST(EditorGizmo, DeactivatingEndsTheDrag)
{
    NS::OS::Input& input = NS::OS::Input::Get();
    input.Mouse().ClearState();
    const int startX = input.Mouse().GetX();
    const int startY = input.Mouse().GetY();
    const NS::Editor::ViewRect view{.x = 0, .y = 0, .width = 320, .height = 200};
    const NS::Matrix viewProjection = MakeViewProjection(view);
    const std::optional<NS::Vector2> handle = FindMoveHandle(viewProjection, view);
    ASSERT_TRUE(handle.has_value());

    NS::Transform target;
    NS::ActorList actors;
    NS::Editor::GizmoEditor gizmo;
    gizmo.SetInput(&input);
    gizmo.SetActive(true);
    gizmo.SetSelected(&target);
    input.Mouse().OnMove(static_cast<int>(handle->x), static_cast<int>(handle->y));
    NS::OS::InputReader reader;
    input.Mouse().OnButtonDown(NS::OS::MouseButton::Left);
    gizmo.Tick(viewProjection, view, true, false, reader.Read(), actors);
    ASSERT_TRUE(gizmo.IsDragging());

    gizmo.SetActive(false);
    EXPECT_FALSE(gizmo.IsDragging());
    // 共有の入力を始めの位置と押していない状態へ戻す。残すと後の試しがマウスの動きを読む
    input.Mouse().OnMove(startX, startY);
    input.Mouse().ClearState();
    input.Mouse().Update();
}

TEST(EditorGizmo, BatchedMeshBoundsDoNotBlockSmallObjects)
{
    NS::Application app(HiddenAppDesc("大きな地形の内側で小物を選ぶ試し", {320, 200}));
    ASSERT_TRUE(app.IsValid());
    app.AddLayer(std::make_unique<TestCheckLayer>([&app] {
        const std::array<NS::StaticVertex, 6> vertices{{
            {{-20.0f, -20.0f, -20.0f}, {}, NS::Vector3::UnitX},
            {{-20.0f, 20.0f, 20.0f}, {}, NS::Vector3::UnitX},
            {{-20.0f, 20.0f, -20.0f}, {}, NS::Vector3::UnitX},
            {{20.0f, -20.0f, -20.0f}, {}, -NS::Vector3::UnitX},
            {{20.0f, -20.0f, 20.0f}, {}, -NS::Vector3::UnitX},
            {{20.0f, 20.0f, 20.0f}, {}, -NS::Vector3::UnitX},
        }};
        const std::array<NS::uint32, 6> indices{0, 1, 2, 3, 4, 5};
        const NS::Gfx::D3DStaticMeshDesc meshDesc{.vertices = vertices.data(),
                                                .vertexCount = vertices.size(),
                                                .indices = indices.data(),
                                                .indexCount = indices.size()};
        std::unique_ptr<NS::Gfx::D3DStaticMesh> drawable = NS::Gfx::D3DStaticMesh::Create(meshDesc);
        ASSERT_NE(drawable, nullptr);
        std::unique_ptr<NS::Phys::MeshShape> triangles = std::make_unique<NS::Phys::MeshShape>();
        triangles->triangles = {{vertices[0].position, vertices[1].position, vertices[2].position},
                                {vertices[3].position, vertices[4].position, vertices[5].position}};
        NS::Mesh batchMesh("SelectionGap", std::move(drawable), std::move(triangles), {});
        NS::Scene scene;
        NS::MapPartsActor* batch = SpawnFromActorFile<NS::MapPartsActor>(scene, "MapPartsActor", "BatchedTerrain");
        NS::MapPartsActor* smallObject = SpawnFromActorFile<NS::MapPartsActor>(scene, "MapPartsActor", "SmallObject");
        ASSERT_NE(batch->ModelClassObj(), nullptr);
        batch->ModelClassObj()->SetMesh(&batchMesh);
        NS::Editor::GizmoEditor gizmo;
        gizmo.SetInput(&app.Input());
        gizmo.SetActive(true);
        const NS::Editor::ViewRect view{.x = 0, .y = 0, .width = 320, .height = 200};
        const NS::Matrix camera = NS::Matrix::CreateLookAt(
            NS::Vector3{0.0f, 0.0f, -30.0f}, NS::Vector3::Zero, NS::Vector3::Up);
        const NS::Matrix projection = NS::Matrix::CreatePerspectiveFieldOfView(1.0f, 1.6f, 0.1f, 100.0f);
        NS::OS::Mouse& mouse = app.Input().Mouse();
        const int startX = mouse.GetX();
        const int startY = mouse.GetY();
        mouse.ClearState();
        NS::OS::InputReader reader;
        mouse.OnMove(view.width / 2, view.height / 2);
        mouse.OnButtonDown(NS::OS::MouseButton::Left);
        gizmo.Tick(camera * projection, view, true, false, reader.Read(), scene.Actors());
        EXPECT_EQ(gizmo.Selected(), &smallObject->Root());
        mouse.OnMove(startX, startY);
        mouse.ClearState();
        mouse.Update();
    }));
    EXPECT_EQ(app.Run(), 0);
}

TEST(EditorGizmo, MeshSurfaceDistanceDecidesWhichObjectIsInFront)
{
    NS::Phys::MeshShape terrain = PickSurface(20.0f);
    const NS::Phys::MeshShape object = PickSurface(5.0f);
    const std::array<NS::Matrix, 2> worlds{NS::Matrix::Identity, NS::Matrix::Identity};
    const std::array<NS::AABB, 2> bounds{
        NS::AABB{{0.0f, 0.0f, 15.5f}, {10.0f, 10.0f, 14.5f}},
        NS::AABB{{0.0f, 0.0f, 5.0f}, {1.0f, 1.0f, 0.1f}}};
    const std::array<const NS::Phys::MeshShape*, 2> meshes{&terrain, &object};
    const NS::Ray ray{NS::Vector3::Zero, NS::Vector3::UnitZ};
    EXPECT_EQ(NS::Editor::GizmoEditor::PickNearestOBB(ray, worlds, bounds, {}, meshes), 1);
    terrain = PickSurface(2.0f);
    EXPECT_EQ(NS::Editor::GizmoEditor::PickNearestOBB(ray, worlds, bounds, {}, meshes), 0);
    const std::array<NS::uint8, 2> mask{0, 1};
    EXPECT_EQ(NS::Editor::GizmoEditor::PickNearestOBB(ray, worlds, bounds, mask, meshes), 1);
}

TEST(EditorGizmo, MeshPickingUsesMeshCoordinatesAndWorldDistanceAfterNonUniformScale)
{
    const NS::Phys::MeshShape farSurface = PickSurface(1.0f, 3.0f);
    const NS::Phys::MeshShape nearSurface = PickSurface(2.0f, 3.0f);
    const std::array<NS::Matrix, 2> worlds{
        NS::Matrix::CreateScale(2.0f, 3.0f, 10.0f) * NS::Matrix::CreateTranslation(-6.0f, 0.0f, 0.0f),
        NS::Matrix::CreateScale(4.0f, 2.0f, 2.0f) * NS::Matrix::CreateTranslation(-12.0f, 0.0f, 0.0f)};
    const std::array<NS::AABB, 2> bounds{
        NS::AABB{{3.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.1f}},
        NS::AABB{{3.0f, 0.0f, 2.0f}, {1.0f, 1.0f, 0.1f}}};
    const std::array<const NS::Phys::MeshShape*, 2> meshes{&farSurface, &nearSurface};
    const NS::Ray ray{NS::Vector3::Zero, NS::Vector3::UnitZ};
    EXPECT_EQ(NS::Editor::GizmoEditor::PickNearestOBB(ray, worlds, bounds, {}, meshes), 1);
}

TEST(EditorGizmo, MeshPickingSupportsRotationAndEitherTriangleSide)
{
    const NS::Phys::MeshShape surface = PickSurface(0.0f);
    const NS::Matrix orientation = NS::Matrix::CreateScale(2.0f, 3.0f, 0.5f) *
                                   NS::Matrix::CreateRotationY(NS::k_Pi * 0.5f);
    const std::array<NS::Matrix, 2> worlds{
        orientation * NS::Matrix::CreateTranslation(5.0f, 0.0f, 0.0f),
        orientation * NS::Matrix::CreateTranslation(8.0f, 0.0f, 0.0f)};
    const std::array<NS::AABB, 2> bounds{
        NS::AABB{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.1f}},
        NS::AABB{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.1f}}};
    const std::array<const NS::Phys::MeshShape*, 2> meshes{&surface, &surface};
    EXPECT_EQ(NS::Editor::GizmoEditor::PickNearestOBB(
                  NS::Ray{NS::Vector3::Zero, NS::Vector3::UnitX}, worlds, bounds, {}, meshes),
              0);
    EXPECT_EQ(NS::Editor::GizmoEditor::PickNearestOBB(
                  NS::Ray{{10.0f, 0.0f, 0.0f}, -NS::Vector3::UnitX}, worlds, bounds, {}, meshes),
              1);
}

TEST(EditorGizmo, EmptyMeshDoesNotBecomeABoxButObjectsWithoutMeshKeepTheirBox)
{
    const NS::Phys::MeshShape emptyMesh;
    const std::array<NS::Matrix, 1> worlds{NS::Matrix::Identity};
    const std::array<NS::AABB, 1> bounds{NS::AABB{{0.0f, 0.0f, 5.0f}, {1.0f, 1.0f, 1.0f}}};
    const std::array<const NS::Phys::MeshShape*, 1> empty{&emptyMesh};
    const std::array<const NS::Phys::MeshShape*, 1> noMesh{nullptr};
    const NS::Ray ray{NS::Vector3::Zero, NS::Vector3::UnitZ};
    EXPECT_EQ(NS::Editor::GizmoEditor::PickNearestOBB(ray, worlds, bounds, {}, empty), -1);
    EXPECT_EQ(NS::Editor::GizmoEditor::PickNearestOBB(ray, worlds, bounds, {}, noMesh), 0);
    EXPECT_EQ(NS::Editor::GizmoEditor::PickNearestOBB(ray, worlds, bounds), 0);
}
