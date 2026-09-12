#include "Editor/LevelEditorController.h"
#include "Runtime/Core/Filesystem.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/Components/MeshColliderComponent.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>

namespace
{
    NS::Object::GameObject* FindFirstPlaced(NS::Object::ObjectList& objects)
    {
        for (NS::Object::GameObject* obj : objects)
        {
            if (!obj->IsTransient())
                return obj;
        }
        return nullptr;
    }
} // namespace

// エディタへ落とした glTF は箱でなく、 自分の三角形で当たる MeshColliderComponent を持つ
TEST(EditorMeshDrop, DroppedMeshCollidesWithItsOwnTriangles)
{
    NS::Object::Scene scene;
    LevelEditorController editor(&scene);

    editor.AddObjectWithMesh(NS::Core::FileSystem::ContentRoot() / "Assets" / "Models" / "ns_drop_terrain.glb");

    NS::Object::GameObject* placed = FindFirstPlaced(scene.Objects());
    ASSERT_NE(placed, nullptr);

    const auto* renderer = placed->FindComponent<NS::Object::MeshRendererComponent>();
    ASSERT_NE(renderer, nullptr);
    EXPECT_EQ(renderer->MeshRef(), "Assets/Models/ns_drop_terrain.glb");
    EXPECT_NE(placed->FindComponent<NS::Object::MeshColliderComponent>(), nullptr);
    EXPECT_EQ(placed->FindComponent<NS::Object::BoxColliderComponent>(), nullptr);
}
