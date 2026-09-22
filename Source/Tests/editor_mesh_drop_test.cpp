#include "Editor/LevelEditorController.h"
#include "Runtime/Platform/Filesystem.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/Components/MeshColliderComponent.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>

namespace
{
    NS::Obj::GameObject* FindFirstPlaced(NS::Obj::ObjectList& objects)
    {
        for (NS::Obj::GameObject* obj : objects)
        {
            if (!obj->IsTransient())
                return obj;
        }
        return nullptr;
    }
} // namespace

// エディタへ落とした glTF は箱でなく、自分の三角形で当たる MeshColliderComponent を持つ
// 参照の実体化は AssetManager を差した Scene だけが行う。この試しは差していないので実在しない file で足りる
TEST(EditorMeshDrop, DroppedMeshCollidesWithItsOwnTriangles)
{
    NS::Obj::Scene scene;
    LevelEditorController editor(&scene);

    editor.AddObjectWithMesh(NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::ContentRoot(), "Assets"), "Models"), "__ns_missing_terrain__.glb"));

    NS::Obj::GameObject* placed = FindFirstPlaced(scene.Objects());
    ASSERT_NE(placed, nullptr);

    const auto* renderer = placed->FindComponent<NS::Obj::MeshRendererComponent>();
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(renderer->MeshRef(), "Assets/Models/__ns_missing_terrain__.glb");
    EXPECT_NE(placed->FindComponent<NS::Obj::MeshColliderComponent>(), nullptr);
    EXPECT_EQ(placed->FindComponent<NS::Obj::BoxColliderComponent>(), nullptr);
}
