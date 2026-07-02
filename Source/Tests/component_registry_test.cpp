#include <gtest/gtest.h>

#include <Framework/Scene/Component.h>
#include <Framework/Scene/ComponentRegistry.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/Reflection.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using NS::Scene::Component;
    using NS::Scene::CreateComponent;
    using NS::Scene::GameObject;
    using NS::Scene::IsRegistered;
    using NS::Scene::ReflectionInfo;
    using NS::Scene::RegisteredNames;

    // curated 型を 1 つ生成し、 attach 先 obj の Components() が 1 増えて末尾が戻り値と一致するのを確かめる
    Component* CreateAndExpectAttached(std::string_view typeName, GameObject& obj)
    {
        const std::size_t before = obj.Components().size();
        Component* comp = CreateComponent(typeName, obj);
        EXPECT_NE(comp, nullptr) << typeName;
        if (comp != nullptr)
        {
            EXPECT_EQ(obj.Components().size(), before + 1) << typeName;
            EXPECT_EQ(obj.Components().back(), comp) << typeName;
        }
        return comp;
    }

    [[nodiscard]] bool Contains(const std::vector<std::string>& names, const char* target)
    {
        for (const std::string& name : names)
        {
            if (name == target)
                return true;
        }
        return false;
    }
} // namespace

// 登録カバレッジの一覧。自己登録 TU がリンカに落とされたり登録マクロが消えたりすると、
// この型の生成が失敗して露見する
TEST(ComponentRegistryTest, CreatesEachCuratedType)
{
    const char* kCurated[] = {
        "BoxColliderComponent",
        "SphereColliderComponent",
        "CapsuleColliderComponent",
        "SlopeColliderComponent",
        "HazardComponent",
        "MeshRendererComponent",
        "PickupComponent",
    };
    for (const char* name : kCurated)
    {
        GameObject obj;
        CreateAndExpectAttached(name, obj);
    }
}

TEST(ComponentRegistryTest, CreatedTypeNameMatchesReflection)
{
    // 登録済み全型で反射 typeName が登録キーと一致する (JSON の type キーと整合)
    // 登録が増えても手直し不要なよう、一覧は registry 自身から取る
    for (const std::string& name : RegisteredNames())
    {
        GameObject obj;
        Component* comp = CreateComponent(name, obj);
        ASSERT_NE(comp, nullptr) << name;
        const ReflectionInfo* info = comp->GetReflection();
        ASSERT_NE(info, nullptr) << name;
        EXPECT_STREQ(info->typeName, name.c_str());
    }
}

TEST(ComponentRegistryTest, ExcludedTypesReturnNull)
{
    GameObject obj;
    EXPECT_EQ(CreateComponent("CameraComponent", obj), nullptr);
    EXPECT_EQ(CreateComponent("PlayerInputComponent", obj), nullptr);
    EXPECT_EQ(CreateComponent("CharacterMovementComponent", obj), nullptr);
    EXPECT_EQ(CreateComponent("EditorCameraComponent", obj), nullptr);
    EXPECT_EQ(obj.Components().size(), 0u);
}

TEST(ComponentRegistryTest, UnknownTypeReturnsNull)
{
    GameObject obj;
    EXPECT_EQ(CreateComponent("Bogus", obj), nullptr);
    EXPECT_EQ(CreateComponent("", obj), nullptr);
    EXPECT_EQ(obj.Components().size(), 0u);
}

TEST(ComponentRegistryTest, IsRegisteredReflectsCuratedSet)
{
    EXPECT_TRUE(IsRegistered("BoxColliderComponent"));
    EXPECT_FALSE(IsRegistered("PlayerInputComponent"));
    EXPECT_FALSE(IsRegistered("CameraComponent"));
    EXPECT_FALSE(IsRegistered("Bogus"));
}

TEST(ComponentRegistryTest, RegisteredNamesListsCuratedSeven)
{
    const std::vector<std::string>& names = RegisteredNames();
    EXPECT_EQ(names.size(), 7u);
    EXPECT_TRUE(Contains(names, "BoxColliderComponent"));
    EXPECT_TRUE(Contains(names, "MeshRendererComponent"));
    EXPECT_TRUE(Contains(names, "PickupComponent"));
    EXPECT_FALSE(Contains(names, "CameraComponent"));
    // パレット表示が実行ごとに揺れない保証。map 由来の一覧は名前順に揃えてある
    EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
}
