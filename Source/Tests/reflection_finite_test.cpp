#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <string>
#include <vector>

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

    // 範囲を付けた float の欄と、点の値を持つ曲線の欄を持つ値型
    struct RangedFields
    {
        float bounded = 5.0f;
        float positive = 1.0f;
        NS::Curve curve{};

        NS_REFLECT_BEGIN(RangedFields, void)
        NS_FIELD(bounded, "1 から 10", NS::FieldRange{1.0f, 10.0f})
        NS_FIELD(positive, "正の数", NS::FieldRange::Positive())
        NS_FIELD(curve, "曲線")
        NS_REFLECT_END_VALUE()
    };

    // 欄が見つからなければ空を返す
    template <class Target, class Value>
    std::optional<NS::FieldWriteResult> WriteField(Target& target, const char* name, const Value& value)
    {
        const NS::FieldDesc* field = NS::FindField(Target::StaticReflection(), name);
        EXPECT_NE(field, nullptr);
        if (field == nullptr)
        {
            return std::nullopt;
        }
        return field->set(&target, &value);
    }

    [[nodiscard]] NS::Curve MakeTwoKeyCurve(float secondY)
    {
        NS::Curve curve{};
        curve.keys[0] = NS::Curve::Key{0.0f, 0.0f};
        curve.keys[1] = NS::Curve::Key{1.0f, secondY};
        curve.count = 2;
        return curve;
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
        EXPECT_EQ(field->set(box, &value), NS::FieldWriteResult::NonFinite);
        EXPECT_EQ(box->HalfExtents(), kept);
    }

    const NS::Vector3 written{4.0f, 5.0f, 6.0f};
    EXPECT_EQ(field->set(box, &written), NS::FieldWriteResult::Written);
    EXPECT_EQ(box->HalfExtents(), written);
}

// float の組の型はどれも、1 成分でも非有限なら書かずに元の値を残す
TEST(ReflectionFinite, FloatCompositeFieldsRejectNonFiniteComponents)
{
    FiniteFields fields;
    const FiniteFields kept = fields;

    const NS::FieldWriteResult nonFinite = NS::FieldWriteResult::NonFinite;
    EXPECT_EQ(WriteField(fields, "数", k_NaN), nonFinite);
    EXPECT_EQ(WriteField(fields, "2 成分", NS::Vector2{1.0f, k_Inf}), nonFinite);
    EXPECT_EQ(WriteField(fields, "3 成分", NS::Vector3{k_NaN, 0.0f, 0.0f}), nonFinite);
    EXPECT_EQ(WriteField(fields, "回転", NS::Quaternion{0.0f, 0.0f, 0.0f, k_Inf}), nonFinite);
    EXPECT_EQ(WriteField(fields, "色", NS::Color{0.0f, k_NaN, 0.0f, 1.0f}), nonFinite);
    EXPECT_EQ(WriteField(fields, "空を持てる色", std::optional<NS::Color>{NS::Color{0.0f, 0.0f, -k_Inf, 1.0f}}),
              nonFinite);
    EXPECT_EQ(WriteField(fields, "読み書きの関数", NS::Vector3{0.0f, k_Inf, 0.0f}), nonFinite);

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

    const NS::FieldWriteResult written = NS::FieldWriteResult::Written;
    EXPECT_EQ(WriteField(fields, "2 成分", NS::Vector2{7.0f, 8.0f}), written);
    EXPECT_EQ(WriteField(fields, "3 成分", NS::Vector3{7.0f, 8.0f, 9.0f}), written);
    EXPECT_EQ(WriteField(fields, "読み書きの関数", NS::Vector3{4.0f, 5.0f, 6.0f}), written);
    EXPECT_EQ(WriteField(fields, "空を持てる色", std::optional<NS::Color>{}), written);

    EXPECT_EQ(fields.pair, (NS::Vector2{7.0f, 8.0f}));
    EXPECT_EQ(fields.triple, (NS::Vector3{7.0f, 8.0f, 9.0f}));
    EXPECT_EQ(fields.accessed, (NS::Vector3{4.0f, 5.0f, 6.0f}));
    EXPECT_FALSE(fields.optionalColor.has_value());
}

// 範囲の内は書き、範囲の外は書かずに今の値を残す。範囲は両端を含む
TEST(ReflectionFinite, RangedFloatFieldWritesInsideAndKeepsValueOutside)
{
    RangedFields fields;

    EXPECT_EQ(WriteField(fields, "1 から 10", 10.0f), NS::FieldWriteResult::Written);
    EXPECT_EQ(fields.bounded, 10.0f);

    EXPECT_EQ(WriteField(fields, "1 から 10", 10.5f), NS::FieldWriteResult::OutOfRange);
    EXPECT_EQ(WriteField(fields, "1 から 10", 0.5f), NS::FieldWriteResult::OutOfRange);
    EXPECT_EQ(fields.bounded, 10.0f);
}

// 正の数の範囲は 0 を範囲の外に数える
TEST(ReflectionFinite, PositiveRangeRejectsZero)
{
    RangedFields fields;

    EXPECT_EQ(WriteField(fields, "正の数", 0.0f), NS::FieldWriteResult::OutOfRange);
    EXPECT_EQ(WriteField(fields, "正の数", -1.0f), NS::FieldWriteResult::OutOfRange);
    EXPECT_EQ(fields.positive, 1.0f);
}

// 非数は範囲より先に見て、範囲の外でなく非有限と返す
TEST(ReflectionFinite, RangedFloatFieldReportsNonFiniteBeforeRange)
{
    RangedFields fields;

    EXPECT_EQ(WriteField(fields, "1 から 10", k_NaN), NS::FieldWriteResult::NonFinite);
    EXPECT_EQ(WriteField(fields, "正の数", k_Inf), NS::FieldWriteResult::NonFinite);
    EXPECT_EQ(fields.bounded, 5.0f);
    EXPECT_EQ(fields.positive, 1.0f);
}

// 曲線は使う点の値と傾きのどれかが非有限なら書かずに今の曲線を残す
TEST(ReflectionFinite, CurveFieldRejectsNonFiniteKey)
{
    RangedFields fields;
    fields.curve = MakeTwoKeyCurve(1.0f);
    const NS::Curve kept = fields.curve;

    EXPECT_EQ(WriteField(fields, "曲線", MakeTwoKeyCurve(k_NaN)), NS::FieldWriteResult::NonFinite);
    NS::Curve brokenTangent = MakeTwoKeyCurve(2.0f);
    brokenTangent.keys[1].inTangent = k_Inf;
    EXPECT_EQ(WriteField(fields, "曲線", brokenTangent), NS::FieldWriteResult::NonFinite);
    EXPECT_EQ(fields.curve, kept);

    // 使わない点の非数は読まない
    NS::Curve unusedBroken = MakeTwoKeyCurve(2.0f);
    unusedBroken.keys[5].y = k_NaN;
    EXPECT_EQ(WriteField(fields, "曲線", unusedBroken), NS::FieldWriteResult::Written);
    EXPECT_EQ(fields.curve.keys[1].y, 2.0f);
}

// 読み込みで書けなかった欄は、欄の名前・読んだ値・理由を 1 件ずつ WARN に出し、今の値を残す
TEST(ReflectionFinite, ReadingRejectedValuesWarnsWithNameValueAndResult)
{
    RangedFields fields;
    const TestLogCapture log;
    // 1e39 は float に収まらず、読むと無限大になる
    const nlohmann::json json = {{"1 から 10", 20.0}, {"正の数", 1.0e39}};
    EXPECT_EQ(NS::ApplyReflectedFields(&fields, *RangedFields::StaticReflection(), json), 0u);

    EXPECT_EQ(fields.bounded, 5.0f);
    EXPECT_EQ(fields.positive, 1.0f);
    const std::vector<std::string> warnings = log.Warnings();
    ASSERT_EQ(warnings.size(), 2u);
    EXPECT_NE(warnings[0].find("1 から 10"), std::string::npos) << warnings[0];
    EXPECT_NE(warnings[0].find("20"), std::string::npos) << warnings[0];
    EXPECT_NE(warnings[0].find("範囲の外"), std::string::npos) << warnings[0];
    EXPECT_NE(warnings[1].find("正の数"), std::string::npos) << warnings[1];
    EXPECT_NE(warnings[1].find("1e+39"), std::string::npos) << warnings[1];
    EXPECT_NE(warnings[1].find("非数か無限大"), std::string::npos) << warnings[1];
}

// 曲線は float に収まらない値の点だけを捨て、残りの点で書く
TEST(ReflectionFinite, ReadingCurveDropsOnlyNonFinitePoint)
{
    RangedFields fields;
    const TestLogCapture log;
    const nlohmann::json json = {{"曲線", {{"curve", {{0.0, 0.0}, {0.5, 1.0e39}, {1.0, 2.0}}}}}};
    EXPECT_EQ(NS::ApplyReflectedFields(&fields, *RangedFields::StaticReflection(), json), 0u);

    ASSERT_EQ(fields.curve.count, 2u);
    EXPECT_EQ(fields.curve.keys[0].x, 0.0f);
    EXPECT_EQ(fields.curve.keys[1].x, 1.0f);
    EXPECT_EQ(log.WarningCount(), 1);
}

// 範囲の内の値を読んでも WARN は出ない
TEST(ReflectionFinite, ReadingValuesInsideRangeDoesNotWarn)
{
    RangedFields fields;
    const TestLogCapture log;
    const nlohmann::json json = {{"1 から 10", 3.0}, {"正の数", 2.0}};
    EXPECT_EQ(NS::ApplyReflectedFields(&fields, *RangedFields::StaticReflection(), json), 0u);

    EXPECT_EQ(fields.bounded, 3.0f);
    EXPECT_EQ(fields.positive, 2.0f);
    EXPECT_EQ(log.WarningCount(), 0);
}
