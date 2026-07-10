#include "Editor/ComponentClipboard.h"
#include "Editor/Undo/AddComponentCommand.h"
#include "Framework/Math/Math.h"
#include "Framework/Scene/Components/BoxColliderComponent.h"
#include "Framework/Scene/Components/CapsuleColliderComponent.h"
#include "Framework/Scene/Components/HazardComponent.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/SceneData.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <variant>
#include <vector>

namespace EditorNs = NS::Editor;
namespace SceneNs = NS::Scene;

TEST(ComponentClipboard, CaptureCopiesTypeAndLiveFieldValues)
{
    SceneNs::GameObject obj;
    auto* capsule = obj.AddComponent<SceneNs::CapsuleColliderComponent>(0.5f, 4.0f);

    const SceneNs::ComponentData captured = EditorNs::CaptureComponentData(*capsule);

    EXPECT_EQ(captured.typeName, "CapsuleColliderComponent");
    ASSERT_EQ(captured.fields.size(), 4u);
    EXPECT_EQ(captured.fields[0].name, "Radius");
    ASSERT_TRUE(std::holds_alternative<float>(captured.fields[0].value));
    EXPECT_FLOAT_EQ(std::get<float>(captured.fields[0].value), 0.5f);
    EXPECT_EQ(captured.fields[1].name, "Half Height");
    ASSERT_TRUE(std::holds_alternative<float>(captured.fields[1].value));
    EXPECT_FLOAT_EQ(std::get<float>(captured.fields[1].value), 4.0f);
}

TEST(ComponentClipboard, CaptureOfFieldlessComponentKeepsTypeWithEmptyFields)
{
    SceneNs::GameObject obj;
    auto* hazard = obj.AddComponent<SceneNs::HazardComponent>();

    const SceneNs::ComponentData captured = EditorNs::CaptureComponentData(*hazard);

    EXPECT_EQ(captured.typeName, "HazardComponent");
    EXPECT_TRUE(captured.fields.empty());
}

TEST(ComponentClipboard, PasteAddsCapturedComponentWithSameValues)
{
    SceneNs::GameObject obj;
    auto* capsule = obj.AddComponent<SceneNs::CapsuleColliderComponent>(0.75f, 6.0f);
    const SceneNs::ComponentData captured = EditorNs::CaptureComponentData(*capsule);

    SceneNs::SceneData lv;
    lv.objects.push_back(SceneNs::ObjectData{});
    SceneNs::EnsureUniqueObjectIds(lv);

    const std::uint32_t id = lv.objects[0].objectId;
    EditorNs::AddComponentCommand paste(id, captured);
    paste.Do(lv);

    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    const SceneNs::ComponentData& pasted = lv.objects[0].components[0];
    EXPECT_EQ(pasted.typeName, "CapsuleColliderComponent");
    ASSERT_EQ(pasted.fields.size(), 4u);
    EXPECT_FLOAT_EQ(std::get<float>(pasted.fields[0].value), 0.75f);
    EXPECT_FLOAT_EQ(std::get<float>(pasted.fields[1].value), 6.0f);

    paste.Undo(lv);
    EXPECT_TRUE(lv.objects[0].components.empty());
}

TEST(ComponentClipboard, WriteBackComponentEditsUpdatesAllMatchingComponentData)
{
    auto findVec3 = [](const SceneNs::ComponentData& data, const char* name) -> const NS::Math::Vector3* {
        for (const SceneNs::FieldValue& field : data.fields)
            if (field.name == name && std::holds_alternative<NS::Math::Vector3>(field.value))
                return &std::get<NS::Math::Vector3>(field.value);
        return nullptr;
    };

    // runtime の collider と MeshRenderer を Inspector で編集した状態を作る
    SceneNs::GameObject go;
    auto* box = go.AddComponent<SceneNs::BoxColliderComponent>();
    box->SetHalfExtents(NS::Math::Vector3{1.0f, 2.0f, 3.0f});
    auto* mesh = go.AddComponent<SceneNs::MeshRendererComponent>(nullptr, nullptr);
    mesh->SetBaseColor(NS::Math::Vector3{0.1f, 0.2f, 0.3f});

    // data 側は旧値の BoxCollider と、 空の MeshRenderer
    SceneNs::ObjectData object;
    SceneNs::ComponentData boxData;
    boxData.typeName = "BoxColliderComponent";
    boxData.fields.push_back(SceneNs::FieldValue{"Half Extents", NS::Math::Vector3{0.5f, 0.5f, 0.5f}});
    object.components.push_back(boxData);
    object.components.push_back(SceneNs::ComponentData{"MeshRendererComponent"});

    EditorNs::WriteBackComponentEdits(go, object);

    // collider が編集後の値で更新される (rebuild が読むのは components 側)
    const NS::Math::Vector3* halfExtents = findVec3(object.components[0], "Half Extents");
    ASSERT_NE(halfExtents, nullptr);
    EXPECT_FLOAT_EQ(halfExtents->x, 1.0f);
    EXPECT_FLOAT_EQ(halfExtents->y, 2.0f);
    EXPECT_FLOAT_EQ(halfExtents->z, 3.0f);

    // collider 以外 (MeshRenderer の Base Color) も書き戻る
    const NS::Math::Vector3* baseColor = findVec3(object.components[1], "Base Color");
    ASSERT_NE(baseColor, nullptr);
    EXPECT_FLOAT_EQ(baseColor->x, 0.1f);
    EXPECT_FLOAT_EQ(baseColor->y, 0.2f);
    EXPECT_FLOAT_EQ(baseColor->z, 0.3f);
}
