#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <gtest/gtest.h>

#include <limits>
#include <optional>

namespace
{
    constexpr float k_Inf = std::numeric_limits<float>::infinity();
    constexpr float k_NaN = std::numeric_limits<float>::quiet_NaN();

    class FiniteBoxActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(FiniteBoxActor, NS::SceneActor)

    protected:
        void OnInit() override { CreateClassObj<NS::Collision, NS::BoxCollision>(m_collision); }
    };

    // float の組を持つ欄の型を、メンバの欄と getter/setter の欄で 1 つずつ持つ値型
    struct FiniteFields
    {
        float scalar = 1.0f;
        NS::Vector2 pair{1.0f, 2.0f};
        NS::Vector3 triple{1.0f, 2.0f, 3.0f};
        NS::Quaternion rotation = NS::Quaternion::Identity;
        NS::Color color{0.1f, 0.2f, 0.3f, 1.0f};
        std::optional<NS::Color> optionalColor = NS::Color{0.4f, 0.5f, 0.6f, 1.0f};
        NS::Vector3 accessed{1.0f, 2.0f, 3.0f};

        [[nodiscard]] NS::Vector3 Accessed() const noexcept { return accessed; }
        void SetAccessed(const NS::Vector3& value) noexcept { accessed = value; }

        NS_REFLECT_BEGIN(FiniteFields, void)
        NS_FIELD(scalar, "数")
        NS_FIELD(pair, "2 成分")
        NS_FIELD(triple, "3 成分")
        NS_FIELD(rotation, "回転")
        NS_FIELD(color, "色")
        NS_FIELD(optionalColor, "空を持てる色")
        NS_ACCESSOR(NS::Vector3, "読み書きの関数", Accessed(), SetAccessed)
        NS_REFLECT_END_VALUE()
    };

    template <class Value> void SetField(FiniteFields& target, const char* name, const Value& value)
    {
        const NS::FieldDesc* field = NS::FindField(FiniteFields::StaticReflection(), name);
        ASSERT_NE(field, nullptr);
        field->set(&target, &value);
    }
} // namespace

// インスペクタ・.actor・シーンに置いたアクターの値は欄の set を通る。非有限の半径は捨て、前の半径を残す
TEST(ReflectionFinite, BoxHalfExtentsFieldKeepsPreviousValueOnNonFinite)
{
    NS::Scene scene;
    FiniteBoxActor* actor = scene.SpawnActor<FiniteBoxActor>("FiniteBoxActor");
    ASSERT_NE(actor, nullptr);
    NS::BoxCollision* box = NS::Cast<NS::BoxCollision>(actor->CollisionClassObj());
    ASSERT_NE(box, nullptr);
    const NS::FieldDesc* field = NS::FindField(box->GetReflection(), "半径");
    ASSERT_NE(field, nullptr);

    const NS::Vector3 kept{1.0f, 2.0f, 3.0f};
    box->SetHalfExtents(kept);
    const NS::Vector3 broken[] = {{k_Inf, 1.0f, 1.0f}, {1.0f, -k_Inf, 1.0f}, {1.0f, 1.0f, k_NaN}};
    for (const NS::Vector3& value : broken)
    {
        field->set(box, &value);
        EXPECT_EQ(box->HalfExtents(), kept);
    }

    const NS::Vector3 written{4.0f, 5.0f, 6.0f};
    field->set(box, &written);
    EXPECT_EQ(box->HalfExtents(), written);
}

// float の組の型はどれも、1 成分でも非有限なら書かずに元の値を残す
TEST(ReflectionFinite, FloatCompositeFieldsRejectNonFiniteComponents)
{
    FiniteFields fields;
    const FiniteFields kept = fields;

    SetField(fields, "数", k_NaN);
    SetField(fields, "2 成分", NS::Vector2{1.0f, k_Inf});
    SetField(fields, "3 成分", NS::Vector3{k_NaN, 0.0f, 0.0f});
    SetField(fields, "回転", NS::Quaternion{0.0f, 0.0f, 0.0f, k_Inf});
    SetField(fields, "色", NS::Color{0.0f, k_NaN, 0.0f, 1.0f});
    SetField(fields, "空を持てる色", std::optional<NS::Color>{NS::Color{0.0f, 0.0f, -k_Inf, 1.0f}});
    SetField(fields, "読み書きの関数", NS::Vector3{0.0f, k_Inf, 0.0f});

    EXPECT_EQ(fields.scalar, kept.scalar);
    EXPECT_EQ(fields.pair, kept.pair);
    EXPECT_EQ(fields.triple, kept.triple);
    EXPECT_EQ(fields.rotation, kept.rotation);
    EXPECT_EQ(fields.color, kept.color);
    EXPECT_EQ(fields.optionalColor, kept.optionalColor);
    EXPECT_EQ(fields.accessed, kept.accessed);
}

// 有限の値と、空の色は書く
TEST(ReflectionFinite, FloatCompositeFieldsWriteFiniteValuesAndEmptyColor)
{
    FiniteFields fields;

    SetField(fields, "2 成分", NS::Vector2{7.0f, 8.0f});
    SetField(fields, "3 成分", NS::Vector3{7.0f, 8.0f, 9.0f});
    SetField(fields, "読み書きの関数", NS::Vector3{4.0f, 5.0f, 6.0f});
    SetField(fields, "空を持てる色", std::optional<NS::Color>{});

    EXPECT_EQ(fields.pair, (NS::Vector2{7.0f, 8.0f}));
    EXPECT_EQ(fields.triple, (NS::Vector3{7.0f, 8.0f, 9.0f}));
    EXPECT_EQ(fields.accessed, (NS::Vector3{4.0f, 5.0f, 6.0f}));
    EXPECT_FALSE(fields.optionalColor.has_value());
}
