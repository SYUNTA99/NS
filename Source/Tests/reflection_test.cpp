#include <gtest/gtest.h>

#include <Framework/Math/Math.h>
#include <Framework/Scene/Component.h>
#include <Framework/Scene/Components/PlacedVirtualCamera.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/Reflection.h>

#include <string_view>

namespace
{
    using NS::Scene::Component;
    using NS::Scene::FieldDesc;
    using NS::Scene::FieldType;
    using NS::Scene::GameObject;
    using NS::Scene::PlacedVirtualCamera;
    using NS::Scene::ReflectionInfo;

    // float / int / bool / Vector3 を private に持ち、 4 フィールドを反射するテスト用 Component
    class FakeReflectedComponent : public Component
    {
    public:
        FakeReflectedComponent() noexcept : Component(0) {}

        NS_REFLECT_BEGIN(FakeReflectedComponent)
        NS_REFLECT_FIELD(m_speed, "Speed")
        NS_REFLECT_FIELD(m_count, "Count")
        NS_REFLECT_FIELD(m_enabled, "Enabled")
        NS_REFLECT_FIELD(m_offset, "Offset")
        NS_REFLECT_END()

        [[nodiscard]] float Speed() const noexcept { return m_speed; }
        [[nodiscard]] int Count() const noexcept { return m_count; }
        [[nodiscard]] bool Enabled() const noexcept { return m_enabled; }
        [[nodiscard]] const NS::Math::Vector3& Offset() const noexcept { return m_offset; }

    private:
        float m_speed = 1.5f;
        int m_count = 3;
        bool m_enabled = true;
        NS::Math::Vector3 m_offset{1.0f, 2.0f, 3.0f};
    };

    // 反射宣言を持たない素の Component 派生
    class BareComponent : public Component
    {
    public:
        BareComponent() noexcept : Component(0) {}
    };

    [[nodiscard]] const FieldDesc* FindField(const ReflectionInfo* info, std::string_view name) noexcept
    {
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (name == info->fields[i].name)
                return &info->fields[i];
        }
        return nullptr;
    }
} // namespace

TEST(ReflectionTest, GetReflectionListsAllFields)
{
    FakeReflectedComponent comp;
    const ReflectionInfo* info = comp.GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 4u);

    const FieldDesc* speed = FindField(info, "Speed");
    const FieldDesc* count = FindField(info, "Count");
    const FieldDesc* enabled = FindField(info, "Enabled");
    const FieldDesc* offset = FindField(info, "Offset");
    ASSERT_NE(speed, nullptr);
    ASSERT_NE(count, nullptr);
    ASSERT_NE(enabled, nullptr);
    ASSERT_NE(offset, nullptr);
    EXPECT_EQ(speed->type, FieldType::Float);
    EXPECT_EQ(count->type, FieldType::Int);
    EXPECT_EQ(enabled->type, FieldType::Bool);
    EXPECT_EQ(offset->type, FieldType::Vector3);
}

TEST(ReflectionTest, GetSetRoundTripsFloat)
{
    FakeReflectedComponent comp;
    const FieldDesc* f = FindField(comp.GetReflection(), "Speed");
    ASSERT_NE(f, nullptr);

    float got = 0.0f;
    f->get(&comp, &got);
    EXPECT_FLOAT_EQ(got, 1.5f);

    float set = 9.25f;
    f->set(&comp, &set);
    EXPECT_FLOAT_EQ(comp.Speed(), 9.25f);
}

TEST(ReflectionTest, GetSetRoundTripsInt)
{
    FakeReflectedComponent comp;
    const FieldDesc* f = FindField(comp.GetReflection(), "Count");
    ASSERT_NE(f, nullptr);

    int got = 0;
    f->get(&comp, &got);
    EXPECT_EQ(got, 3);

    int set = 42;
    f->set(&comp, &set);
    EXPECT_EQ(comp.Count(), 42);
}

TEST(ReflectionTest, GetSetRoundTripsBool)
{
    FakeReflectedComponent comp;
    const FieldDesc* f = FindField(comp.GetReflection(), "Enabled");
    ASSERT_NE(f, nullptr);

    bool got = false;
    f->get(&comp, &got);
    EXPECT_TRUE(got);

    bool set = false;
    f->set(&comp, &set);
    EXPECT_FALSE(comp.Enabled());
}

TEST(ReflectionTest, GetSetRoundTripsVector3)
{
    FakeReflectedComponent comp;
    const FieldDesc* f = FindField(comp.GetReflection(), "Offset");
    ASSERT_NE(f, nullptr);

    NS::Math::Vector3 got{};
    f->get(&comp, &got);
    EXPECT_FLOAT_EQ(got.x, 1.0f);
    EXPECT_FLOAT_EQ(got.y, 2.0f);
    EXPECT_FLOAT_EQ(got.z, 3.0f);

    NS::Math::Vector3 set{4.0f, 5.0f, 6.0f};
    f->set(&comp, &set);
    EXPECT_FLOAT_EQ(comp.Offset().x, 4.0f);
    EXPECT_FLOAT_EQ(comp.Offset().y, 5.0f);
    EXPECT_FLOAT_EQ(comp.Offset().z, 6.0f);
}

TEST(ReflectionTest, BareComponentHasNullReflection)
{
    BareComponent comp;
    EXPECT_EQ(comp.GetReflection(), nullptr);
}

TEST(ReflectionTest, PlacedVirtualCameraReflectsSixFields)
{
    GameObject host;
    auto* cam = host.AddComponent<PlacedVirtualCamera>();
    cam->SetView({1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f});
    cam->SetTrigger({7.0f, 8.0f, 9.0f}, {10.0f, 11.0f, 12.0f});
    cam->SetLookAtPlayer(true);
    cam->SetVcamPriority(15);

    const ReflectionInfo* info = cam->GetReflection();
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->fieldCount, 6u);

    EXPECT_NE(FindField(info, "Camera Pos"), nullptr);
    EXPECT_NE(FindField(info, "Look Target"), nullptr);
    EXPECT_NE(FindField(info, "Trigger Center"), nullptr);
    EXPECT_NE(FindField(info, "Trigger Extent"), nullptr);
    EXPECT_NE(FindField(info, "Look At Player"), nullptr);
    EXPECT_NE(FindField(info, "Priority"), nullptr);
    // up は POD に枠が無いため反射しない
    EXPECT_EQ(FindField(info, "Up"), nullptr);

    // Priority は基底 accessor 経由で書き戻る
    const FieldDesc* priority = FindField(info, "Priority");
    ASSERT_NE(priority, nullptr);
    int newPriority = 7;
    priority->set(cam, &newPriority);
    EXPECT_EQ(cam->VcamPriority(), 7);

    // Trigger Center は直メンバ経由で書き戻る
    const FieldDesc* center = FindField(info, "Trigger Center");
    ASSERT_NE(center, nullptr);
    NS::Math::Vector3 newCenter{20.0f, 21.0f, 22.0f};
    center->set(cam, &newCenter);
    EXPECT_FLOAT_EQ(cam->TriggerCenter().x, 20.0f);
    EXPECT_FLOAT_EQ(cam->TriggerCenter().y, 21.0f);
    EXPECT_FLOAT_EQ(cam->TriggerCenter().z, 22.0f);
}
