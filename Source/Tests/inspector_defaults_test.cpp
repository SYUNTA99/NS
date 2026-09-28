#include <Editor/InspectorReflection.h>
#include <Game/Player/PlayerComponent.h>
#include <Runtime/Object/Component.h>
#include <Runtime/Object/Components/BoxCollider.h>
#include <Runtime/Object/Components/CapsuleCollider.h>
#include <Runtime/Object/Components/MeshCollider.h>
#include <Runtime/Object/Components/MeshRenderer.h>
#include <Runtime/Object/Components/SlopeCollider.h>
#include <Runtime/Object/Components/SphereCollider.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <gtest/gtest.h>

namespace
{
    // 最初の float 欄。どの調整値かに依らずテストを書けるようにする
    const NS::Obj::FieldDesc* FindFloatField(const NS::Obj::ReflectionInfo& info)
    {
        for (std::size_t i = 0; i < info.fieldCount; ++i)
        {
            if (info.fields[i].type == NS::Obj::FieldType::Float)
                return &info.fields[i];
        }
        return nullptr;
    }
} // namespace

TEST(InspectorDefaultsTest, SameTypeReusesOneInstance)
{
    NS::Editor::ComponentDefaults defaults;
    const NS::Obj::Component* first = defaults.Find("PlayerComponent");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(defaults.Find("PlayerComponent"), first);
}

TEST(InspectorDefaultsTest, UnknownTypeHasNoDefaults)
{
    NS::Editor::ComponentDefaults defaults;
    EXPECT_EQ(defaults.Find("NoSuchComponent"), nullptr);
}

TEST(InspectorDefaultsTest, MarkFollowsTheEditedValue)
{
    NS::Editor::ComponentDefaults defaults;
    const NS::Obj::Component* baseline = defaults.Find("PlayerComponent");
    ASSERT_NE(baseline, nullptr);

    NS::Obj::GameObject obj;
    NS::Obj::Component* live = obj.AddComponent<NS::Game::Player::PlayerComponent>();
    ASSERT_NE(live, nullptr);
    const NS::Obj::ReflectionInfo* info = live->GetReflection();
    ASSERT_NE(info, nullptr);
    const NS::Obj::FieldDesc* field = FindFloatField(*info);
    ASSERT_NE(field, nullptr);

    // 置いたままの値は既定と同じなので印を出さない
    EXPECT_FALSE(NS::Editor::FieldDiffersFromDefault(*live, baseline, *field));

    float original = 0.0f;
    field->get(live, &original);
    const float edited = original + 1.0f;
    field->set(live, &edited);
    EXPECT_TRUE(NS::Editor::FieldDiffersFromDefault(*live, baseline, *field));

    NS::Editor::RevertFieldToDefault(*live, *baseline, *field);
    EXPECT_FALSE(NS::Editor::FieldDiffersFromDefault(*live, baseline, *field));
    float restored = 0.0f;
    field->get(live, &restored);
    EXPECT_FLOAT_EQ(restored, original);
}

TEST(InspectorDefaultsTest, NoBaselineMeansNoMark)
{
    NS::Obj::GameObject obj;
    NS::Obj::Component* live = obj.AddComponent<NS::Game::Player::PlayerComponent>();
    ASSERT_NE(live, nullptr);
    const NS::Obj::ReflectionInfo* info = live->GetReflection();
    ASSERT_NE(info, nullptr);
    const NS::Obj::FieldDesc* field = FindFloatField(*info);
    ASSERT_NE(field, nullptr);

    EXPECT_FALSE(NS::Editor::FieldDiffersFromDefault(*live, nullptr, *field));
}

// 当たり判定の欄は Inspector で変えても物理へ届かない。5 つの形はどれも張り直しが要る
TEST(InspectorDefaultsTest, ColliderEditsNeedAPhysicsSync)
{
    NS::Obj::GameObject obj;
    EXPECT_TRUE(NS::Editor::EditNeedsPhysicsSync(*obj.AddComponent<NS::Obj::BoxCollider>()));
    EXPECT_TRUE(NS::Editor::EditNeedsPhysicsSync(*obj.AddComponent<NS::Obj::SphereCollider>()));
    EXPECT_TRUE(NS::Editor::EditNeedsPhysicsSync(*obj.AddComponent<NS::Obj::CapsuleCollider>()));
    EXPECT_TRUE(NS::Editor::EditNeedsPhysicsSync(*obj.AddComponent<NS::Obj::MeshCollider>()));
    EXPECT_TRUE(NS::Editor::EditNeedsPhysicsSync(*obj.AddComponent<NS::Obj::SlopeCollider>()));
    EXPECT_FALSE(NS::Editor::EditNeedsPhysicsSync(*obj.AddComponent<NS::Obj::MeshRenderer>()));
}
