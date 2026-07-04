#include <gtest/gtest.h>

#include <Framework/Scene/Component.h>
#include <Framework/Scene/ComponentRegistry.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/Reflection.h>

#include <algorithm>
#include <cstddef>
#include <map>
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

    // 登録型を 1 つ生成し、 attach 先 obj の Components() が 1 増えて末尾が戻り値と一致するのを確かめる
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
TEST(ComponentRegistryTest, CreatesEachRegisteredType)
{
    const char* kRegistered[] = {
        "BoxColliderComponent",
        "SphereColliderComponent",
        "CapsuleColliderComponent",
        "SlopeColliderComponent",
        "MeshColliderComponent",
        "HazardComponent",
        "MeshRendererComponent",
        "PickupComponent",
        "CameraComponent",
        "PlacedVirtualCamera",
        "CameraBrainComponent",
        "ThirdPersonFollowComponent",
        "CharacterMovementComponent",
        "PlayerInputComponent",
        "ShadowComponent",
        "SkeletalAnimationComponent",
    };
    for (const char* name : kRegistered)
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
    // editor 専用と抽象基底は登録しないので、信頼できない type 名から生成できない
    GameObject obj;
    EXPECT_EQ(CreateComponent("EditorCameraComponent", obj), nullptr);
    EXPECT_EQ(CreateComponent("VirtualCameraComponent", obj), nullptr);
    EXPECT_EQ(obj.Components().size(), 0u);
}

TEST(ComponentRegistryTest, UnknownTypeReturnsNull)
{
    GameObject obj;
    EXPECT_EQ(CreateComponent("Bogus", obj), nullptr);
    EXPECT_EQ(CreateComponent("", obj), nullptr);
    EXPECT_EQ(obj.Components().size(), 0u);
}

TEST(ComponentRegistryTest, IsRegisteredMatchesRegistrationSet)
{
    EXPECT_TRUE(IsRegistered("BoxColliderComponent"));
    EXPECT_TRUE(IsRegistered("CharacterMovementComponent"));
    EXPECT_FALSE(IsRegistered("EditorCameraComponent"));
    EXPECT_FALSE(IsRegistered("Bogus"));
}

TEST(ComponentRegistryTest, RegisteredNamesListsAllRuntimeTypes)
{
    const std::vector<std::string>& names = RegisteredNames();
    EXPECT_EQ(names.size(), 16u);
    EXPECT_TRUE(Contains(names, "BoxColliderComponent"));
    EXPECT_TRUE(Contains(names, "MeshRendererComponent"));
    EXPECT_TRUE(Contains(names, "CharacterMovementComponent"));
    EXPECT_FALSE(Contains(names, "EditorCameraComponent"));
    // パレット表示が実行ごとに揺れない保証。map 由来の一覧は名前順に揃えてある
    EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
}

TEST(ComponentRegistryTest, ReflectedFieldsMatchLedger)
{
    // 反射フィールドの台帳。ここに載ったフィールドだけが Inspector 編集とシリアライズの対象になる
    // 増減が意図か事故かをこの台帳との突き合わせで判定する。抜けは無言のデータ欠損になる
    const std::map<std::string, std::vector<std::string>> kLedger = {
        {"BoxColliderComponent", {"Half Extents", "Center Offset", "Rotation (deg)"}},
        {"CameraBrainComponent", {"Blend Duration"}},
        {"CameraComponent", {}},
        {"CapsuleColliderComponent", {"Radius", "Half Height", "Center Offset", "Rotation (deg)"}},
        {"CharacterMovementComponent",
         {"Jump Impulse",
          "Gravity Up",
          "Gravity Down",
          "Apex Hang Vy",
          "Apex Hang Scale",
          "Jump Release Scale",
          "Coyote Time",
          "Jump Buffer Time",
          "Max Speed",
          "Walk Speed",
          "Accel Tau",
          "Decel Tau",
          "Stick Deadzone",
          "Capsule Radius",
          "Capsule Half Height",
          "Debug Draw"}},
        {"HazardComponent", {}},
        {"MeshColliderComponent", {}},
        {"MeshRendererComponent", {"Base Color", "Mesh", "Material"}},
        {"PickupComponent", {"Pickup Kind"}},
        {"PlacedVirtualCamera",
         {"Look Target", "Up", "Trigger Center", "Trigger Extent", "Look At Player", "Priority"}},
        {"PlayerInputComponent", {}},
        {"ShadowComponent", {"Base Diameter", "Max Drop", "Surface Offset", "Base Alpha"}},
        {"SkeletalAnimationComponent", {"Speed", "Looping"}},
        {"SlopeColliderComponent", {"Angle (deg)", "Half Extents"}},
        {"SphereColliderComponent", {"Radius", "Center Offset"}},
        {"ThirdPersonFollowComponent",
         {"Target",
          "Spring Omega",
          "Idle Distance",
          "Run Distance",
          "Jump Distance",
          "Run Speed Threshold",
          "Head Height",
          "Sensitivity X",
          "Sensitivity Y",
          "Stick Sens X",
          "Stick Sens Y",
          "Invert X",
          "Invert Y",
          "Pitch Min",
          "Pitch Max",
          "Far Plane",
          "Priority"}},
    };

    const std::vector<std::string>& names = RegisteredNames();
    ASSERT_EQ(names.size(), kLedger.size());
    for (const std::string& name : names)
    {
        const auto entry = kLedger.find(name);
        ASSERT_NE(entry, kLedger.end()) << name << " が台帳に無い";

        GameObject obj;
        Component* comp = CreateComponent(name, obj);
        ASSERT_NE(comp, nullptr) << name;
        const ReflectionInfo* info = comp->GetReflection();
        ASSERT_NE(info, nullptr) << name;

        std::vector<std::string> actual;
        actual.reserve(info->fieldCount);
        for (std::size_t i = 0; i < info->fieldCount; ++i)
            actual.emplace_back(info->fields[i].name);
        EXPECT_EQ(actual, entry->second) << name;
    }
}

TEST(ComponentRegistryTest, BaseChainMatchesLedger)
{
    // 基底鎖の台帳。空は Component 直下で鎖が終端することを表す
    // 誤った基底を書いた宣言は typeName 一致では捕まらないため、期待基底を明示して突き合わせる
    const std::map<std::string, std::vector<std::string>> kBaseLedger = {
        {"BoxColliderComponent", {}},
        {"CameraBrainComponent", {}},
        {"CameraComponent", {}},
        {"CapsuleColliderComponent", {}},
        {"CharacterMovementComponent", {}},
        {"HazardComponent", {}},
        {"MeshColliderComponent", {}},
        {"MeshRendererComponent", {}},
        {"PickupComponent", {}},
        {"PlacedVirtualCamera", {"VirtualCameraComponent"}},
        {"PlayerInputComponent", {}},
        {"ShadowComponent", {}},
        {"SkeletalAnimationComponent", {}},
        {"SlopeColliderComponent", {}},
        {"SphereColliderComponent", {}},
        {"ThirdPersonFollowComponent", {"VirtualCameraComponent"}},
    };

    const std::vector<std::string>& names = RegisteredNames();
    ASSERT_EQ(names.size(), kBaseLedger.size());
    for (const std::string& name : names)
    {
        const auto entry = kBaseLedger.find(name);
        ASSERT_NE(entry, kBaseLedger.end()) << name << " が台帳に無い";

        GameObject obj;
        Component* comp = CreateComponent(name, obj);
        ASSERT_NE(comp, nullptr) << name;
        const ReflectionInfo* info = comp->GetReflection();
        ASSERT_NE(info, nullptr) << name;

        std::vector<std::string> actual;
        for (const ReflectionInfo* base = info->base; base != nullptr; base = base->base)
            actual.emplace_back(base->typeName);
        EXPECT_EQ(actual, entry->second) << name;
    }
}
