#include <Game/Level/HazardComponent.h>
#include <gtest/gtest.h>
#include <Runtime/Math/Math.h>
#include <Runtime/Object/Component.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/SlopeColliderComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Reflection/ReflectionJson.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>
#include <string>

namespace
{
    using NS::Object::ApplyJsonFields;
    using NS::Object::Component;
    using NS::Object::CreateComponent;
    using NS::Object::GameObject;
    using NS::Object::ReflectionInfo;
    using NS::Object::SerializeComponent;

    // std::string を反射するテスト用 Component、curated に string 型がまだ無いので自前で用意
    class FakeStringComponent : public Component
    {
    public:
        FakeStringComponent() noexcept : Component(0) {}

        NS_REFLECT_BEGIN(FakeStringComponent, Component)
        NS_REFLECT_FIELD(m_label, "Label")
        NS_REFLECT_END()

        [[nodiscard]] const std::string& Label() const noexcept { return m_label; }

    private:
        std::string m_label{"hello"};
    };

    // ObjectRef を反射するテスト用 Component、curated に ObjectRef 型がまだ無いので自前で用意
    class FakeRefComponent : public Component
    {
    public:
        FakeRefComponent() noexcept : Component(0) {}

        NS_REFLECT_BEGIN(FakeRefComponent, Component)
        NS_REFLECT_FIELD(m_target, "Target")
        NS_REFLECT_END()

        [[nodiscard]] NS::Object::ObjectRef Target() const noexcept { return m_target; }

    private:
        NS::Object::ObjectRef m_target{};
    };
} // namespace

TEST(ReflectionJsonTest, SerializeWritesTypeAndFields)
{
    GameObject obj;
    auto* box = obj.AddComponent<NS::Object::BoxColliderComponent>();

    const nlohmann::json j = SerializeComponent(*box);
    EXPECT_EQ(j["type"], "BoxColliderComponent");
    ASSERT_TRUE(j["fields"].is_object());
    EXPECT_TRUE(j["fields"].contains("Half Extents"));
    EXPECT_TRUE(j["fields"].contains("Center Offset"));
}

TEST(ReflectionJsonTest, RoundTripVector3Fields)
{
    GameObject src;
    auto* box = src.AddComponent<NS::Object::BoxColliderComponent>();
    box->SetHalfExtents(NS::Math::Vector3{2.0f, 3.0f, 4.0f});
    box->SetCenterOffset(NS::Math::Vector3{1.0f, -2.0f, 0.5f});

    const nlohmann::json j = SerializeComponent(*box);

    GameObject dst;
    Component* restored = CreateComponent("BoxColliderComponent", dst);
    ASSERT_NE(restored, nullptr);
    ApplyJsonFields(*restored, j["fields"]);

    auto* restoredBox = static_cast<NS::Object::BoxColliderComponent*>(restored);
    EXPECT_FLOAT_EQ(restoredBox->HalfExtents().x, 2.0f);
    EXPECT_FLOAT_EQ(restoredBox->HalfExtents().y, 3.0f);
    EXPECT_FLOAT_EQ(restoredBox->HalfExtents().z, 4.0f);
    EXPECT_FLOAT_EQ(restoredBox->CenterOffset().x, 1.0f);
    EXPECT_FLOAT_EQ(restoredBox->CenterOffset().y, -2.0f);
    EXPECT_FLOAT_EQ(restoredBox->CenterOffset().z, 0.5f);
}

TEST(ReflectionJsonTest, RoundTripStringField)
{
    FakeStringComponent src;
    nlohmann::json j = SerializeComponent(src);
    EXPECT_EQ(j["fields"]["Label"], "hello");

    // 文字列を書き換えた JSON を別インスタンスへ適用すると set で反映される
    j["fields"]["Label"] = "world";
    FakeStringComponent dst;
    ApplyJsonFields(dst, j["fields"]);
    EXPECT_EQ(dst.Label(), "world");
}

TEST(ReflectionJsonTest, HazardReflectsWithEmptyFields)
{
    NS::Game::Level::HazardComponent hazard;
    const ReflectionInfo* info = hazard.GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 0u);

    const nlohmann::json j = SerializeComponent(hazard);
    EXPECT_EQ(j["type"], "HazardComponent");
    ASSERT_TRUE(j["fields"].is_object());
    EXPECT_TRUE(j["fields"].empty());
}

TEST(ReflectionJsonTest, SlopeReflectsAngleAndRoundTrips)
{
    NS::Object::SlopeColliderComponent slope;
    const ReflectionInfo* info = slope.GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 2u);

    nlohmann::json j = SerializeComponent(slope);
    EXPECT_EQ(j["type"], "SlopeColliderComponent");
    EXPECT_FLOAT_EQ(j["fields"]["Angle (deg)"].get<float>(), 45.0f);

    // 角度を変えた JSON を適用すると set で書き戻る
    j["fields"]["Angle (deg)"] = 30.0f;
    NS::Object::SlopeColliderComponent dst;
    ApplyJsonFields(dst, j["fields"]);
    EXPECT_FLOAT_EQ(dst.AngleDegrees(), 30.0f);
}

TEST(ReflectionJsonTest, UnknownAndMissingKeysAreIgnored)
{
    GameObject obj;
    auto* box = obj.AddComponent<NS::Object::BoxColliderComponent>();
    box->SetHalfExtents(NS::Math::Vector3{2.0f, 2.0f, 2.0f});

    // 未知キー + 欠損 (Half Extents を含まない) + 型不一致を混ぜても落ちず、 既定/現状値が保たれる
    nlohmann::json fields;
    fields["No Such Field"] = 123;
    fields["Center Offset"] = "not an array";
    ApplyJsonFields(*box, fields);

    EXPECT_FLOAT_EQ(box->HalfExtents().x, 2.0f);
    EXPECT_FLOAT_EQ(box->CenterOffset().x, 0.0f);
}

TEST(ReflectionJsonTest, ObjectRefSerializesAsSingleKeyObjectAndRoundTrips)
{
    FakeRefComponent src;
    nlohmann::json j = SerializeComponent(src);
    ASSERT_TRUE(j["fields"]["Target"].is_object());
    EXPECT_EQ(j["fields"]["Target"]["ref"], 0u);

    // id を書き換えた JSON を適用すると set で反映される
    j["fields"]["Target"]["ref"] = 42u;
    FakeRefComponent dst;
    ApplyJsonFields(dst, j["fields"]);
    EXPECT_EQ(dst.Target().id, 42u);
}

TEST(ReflectionJsonTest, ObjectRefRejectsBrokenJsonShapes)
{
    // 素の数値・負数・キー違いはいずれも受け付けず既定 0 のまま
    FakeRefComponent comp;
    nlohmann::json fields;
    fields["Target"] = 7;
    ApplyJsonFields(comp, fields);
    EXPECT_EQ(comp.Target().id, 0u);

    fields["Target"] = nlohmann::json{{"ref", -3}};
    ApplyJsonFields(comp, fields);
    EXPECT_EQ(comp.Target().id, 0u);

    fields["Target"] = nlohmann::json{{"id", 5}};
    ApplyJsonFields(comp, fields);
    EXPECT_EQ(comp.Target().id, 0u);
}
