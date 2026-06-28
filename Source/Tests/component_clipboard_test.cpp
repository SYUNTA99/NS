#include "Editor/ComponentClipboard.h"
#include "Editor/Undo/AddComponentCommand.h"
#include "Framework/Math/Math.h"
#include "Framework/Scene/Components/BoxColliderComponent.h"
#include "Framework/Scene/Components/HazardComponent.h"
#include "Framework/Scene/Components/PoleComponent.h"
#include "Framework/Scene/GameObject.h"
#include "Game/Level/EditTarget.h"
#include "Game/Level/LevelData.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <variant>
#include <vector>

namespace EditorNs = NS::Editor;
namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Scene;

TEST(ComponentClipboard, CaptureCopiesTypeAndLiveFieldValues)
{
    SceneNs::GameObject obj;
    auto* pole = obj.AddComponent<SceneNs::PoleComponent>(0.5f, 4.0f);

    const LevelNs::ComponentData captured = EditorNs::CaptureComponentData(*pole);

    EXPECT_EQ(captured.typeName, "PoleComponent");
    ASSERT_EQ(captured.fields.size(), 2u);
    EXPECT_EQ(captured.fields[0].name, "Radius");
    ASSERT_TRUE(std::holds_alternative<float>(captured.fields[0].value));
    EXPECT_FLOAT_EQ(std::get<float>(captured.fields[0].value), 0.5f);
    EXPECT_EQ(captured.fields[1].name, "Height");
    ASSERT_TRUE(std::holds_alternative<float>(captured.fields[1].value));
    EXPECT_FLOAT_EQ(std::get<float>(captured.fields[1].value), 4.0f);
}

TEST(ComponentClipboard, CaptureOfFieldlessComponentKeepsTypeWithEmptyFields)
{
    SceneNs::GameObject obj;
    auto* hazard = obj.AddComponent<SceneNs::HazardComponent>();

    const LevelNs::ComponentData captured = EditorNs::CaptureComponentData(*hazard);

    EXPECT_EQ(captured.typeName, "HazardComponent");
    EXPECT_TRUE(captured.fields.empty());
}

TEST(ComponentClipboard, PasteAddsCapturedComponentWithSameValues)
{
    SceneNs::GameObject obj;
    auto* pole = obj.AddComponent<SceneNs::PoleComponent>(0.75f, 6.0f);
    const LevelNs::ComponentData captured = EditorNs::CaptureComponentData(*pole);

    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::ObjectInstance{});
    std::vector<std::uint32_t> ids;
    std::uint32_t next = 0;
    LevelNs::EditTarget t{lv, ids, next};
    LevelNs::ResetEditIds(t);

    const std::uint32_t id = LevelNs::IdAt(t, 0);
    EditorNs::AddComponentCommand paste(id, captured);
    paste.Do(t);

    ASSERT_EQ(lv.objects[0].components.size(), 1u);
    const LevelNs::ComponentData& pasted = lv.objects[0].components[0];
    EXPECT_EQ(pasted.typeName, "PoleComponent");
    ASSERT_EQ(pasted.fields.size(), 2u);
    EXPECT_FLOAT_EQ(std::get<float>(pasted.fields[0].value), 0.75f);
    EXPECT_FLOAT_EQ(std::get<float>(pasted.fields[1].value), 6.0f);

    paste.Undo(t);
    EXPECT_TRUE(lv.objects[0].components.empty());
}

TEST(ComponentClipboard, WriteBackColliderEditsUpdatesMatchingComponentData)
{
    // runtime collider を Inspector で編集した状態を作る
    SceneNs::GameObject go;
    auto* box = go.AddComponent<SceneNs::BoxColliderComponent>();
    box->SetHalfExtents(NS::Math::Vector3{1.0f, 2.0f, 3.0f});

    // data 側は旧 half-extents の BoxCollider と、 書き戻し対象外の MeshRenderer
    LevelNs::ObjectInstance object;
    object.components.push_back(LevelNs::ComponentData{"MeshRendererComponent"});
    LevelNs::ComponentData boxData;
    boxData.typeName = "BoxColliderComponent";
    boxData.fields.push_back(LevelNs::FieldValue{"Half Extents", NS::Math::Vector3{0.5f, 0.5f, 0.5f}});
    object.components.push_back(boxData);

    EditorNs::WriteBackColliderEdits(go, object);

    // collider データが編集後の値で更新される (rebuild が読むのは components 側)
    const LevelNs::ComponentData& updated = object.components[1];
    ASSERT_EQ(updated.typeName, "BoxColliderComponent");
    const NS::Math::Vector3* halfExtents = nullptr;
    for (const LevelNs::FieldValue& field : updated.fields)
        if (field.name == "Half Extents" && std::holds_alternative<NS::Math::Vector3>(field.value))
            halfExtents = &std::get<NS::Math::Vector3>(field.value);
    ASSERT_NE(halfExtents, nullptr);
    EXPECT_FLOAT_EQ(halfExtents->x, 1.0f);
    EXPECT_FLOAT_EQ(halfExtents->y, 2.0f);
    EXPECT_FLOAT_EQ(halfExtents->z, 3.0f);

    // collider 以外のコンポーネントは触られない
    EXPECT_EQ(object.components[0].typeName, "MeshRendererComponent");
    EXPECT_TRUE(object.components[0].fields.empty());
}
