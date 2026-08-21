#include <Editor/InspectorReflection.h>
#include <Runtime/Object/Component.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Curve.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Reflection/ReflectionJson.h>
#include <gtest/gtest.h>

#include <initializer_list>

namespace
{
    using NS::Object::Component;
    using NS::Object::Curve;
    using NS::Object::FieldDesc;
    using NS::Object::FieldType;
    using NS::Object::FindField;

    class FakeCurveComponent : public Component
    {
    public:
        FakeCurveComponent() noexcept : Component(0) {}

        NS_REFLECT_BEGIN(FakeCurveComponent, Component)
        NS_REFLECT_FIELD(m_curve, "カーブ")
        NS_REFLECT_END()

        [[nodiscard]] const Curve& Shape() const noexcept { return m_curve; }
        void SetShape(const Curve& curve) noexcept { m_curve = curve; }

    private:
        Curve m_curve{};
    };

    Curve MakeCurve(std::initializer_list<Curve::Key> keys)
    {
        Curve curve{};
        for (const Curve::Key& key : keys)
        {
            curve.keys[curve.count] = key;
            ++curve.count;
        }
        return curve;
    }
} // namespace

TEST(CurveReflection, EvaluateWithNoKeysReturnsZero)
{
    const Curve curve{};
    EXPECT_FLOAT_EQ(curve.Evaluate(0.5f), 0.0f);
}

TEST(CurveReflection, EvaluateWithOneKeyReturnsItsValueEverywhere)
{
    const Curve curve = MakeCurve({{0.0f, 3.0f}});
    EXPECT_FLOAT_EQ(curve.Evaluate(-10.0f), 3.0f);
    EXPECT_FLOAT_EQ(curve.Evaluate(10.0f), 3.0f);
}

TEST(CurveReflection, EvaluateInterpolatesBetweenKeys)
{
    const Curve curve = MakeCurve({{0.0f, 1.0f}, {1.0f, 3.0f}});
    EXPECT_FLOAT_EQ(curve.Evaluate(0.5f), 2.0f);
}

TEST(CurveReflection, EvaluateOutsideRangeReturnsEndValues)
{
    const Curve curve = MakeCurve({{0.0f, 1.0f}, {1.0f, 3.0f}});
    EXPECT_FLOAT_EQ(curve.Evaluate(-1.0f), 1.0f);
    EXPECT_FLOAT_EQ(curve.Evaluate(2.0f), 3.0f);
}

TEST(CurveReflection, SortKeysOrdersAscendingByX)
{
    Curve curve = MakeCurve({{2.0f, 20.0f}, {0.0f, 0.0f}, {1.0f, 10.0f}});
    curve.SortKeys();
    ASSERT_EQ(curve.count, 3u);
    EXPECT_FLOAT_EQ(curve.keys[0].x, 0.0f);
    EXPECT_FLOAT_EQ(curve.keys[0].y, 0.0f);
    EXPECT_FLOAT_EQ(curve.keys[1].x, 1.0f);
    EXPECT_FLOAT_EQ(curve.keys[1].y, 10.0f);
    EXPECT_FLOAT_EQ(curve.keys[2].x, 2.0f);
    EXPECT_FLOAT_EQ(curve.keys[2].y, 20.0f);
}

TEST(CurveReflection, SameShapesCompareEqual)
{
    const Curve a = MakeCurve({{0.0f, 1.0f}, {1.0f, 3.0f}});
    const Curve b = MakeCurve({{0.0f, 1.0f}, {1.0f, 3.0f}});
    const Curve c = MakeCurve({{0.0f, 1.0f}, {1.0f, 4.0f}});
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(CurveReflection, FieldTypeOfCurveIsCurve)
{
    EXPECT_EQ(NS::Object::FieldTypeOf<Curve>(), FieldType::Curve);
}

TEST(CurveReflection, ReflectedFieldHasCurveTypeAndRoundTrips)
{
    FakeCurveComponent comp;
    const FieldDesc* field = FindField(comp.GetReflection(), "カーブ");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, FieldType::Curve);

    const Curve in = MakeCurve({{0.0f, 1.0f}, {1.0f, 3.0f}});
    field->set(&comp, &in);
    Curve out{};
    field->get(&comp, &out);
    EXPECT_TRUE(out == in);
}

TEST(CurveReflection, SerializeWritesSingleKeyCurveObject)
{
    FakeCurveComponent comp;
    comp.SetShape(MakeCurve({{0.0f, 1.0f}, {1.0f, 3.0f}, {2.0f, 0.5f}}));

    const nlohmann::json out = NS::Object::SerializeComponent(comp);
    const nlohmann::json& field = out["fields"]["カーブ"];
    ASSERT_TRUE(field.is_object());
    ASSERT_TRUE(field.contains("curve"));
    const nlohmann::json& points = field["curve"];
    ASSERT_TRUE(points.is_array());
    ASSERT_EQ(points.size(), 3u);
    ASSERT_TRUE(points[1].is_array());
    ASSERT_EQ(points[1].size(), 2u);
    EXPECT_FLOAT_EQ(points[1][0].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(points[1][1].get<float>(), 3.0f);
}

TEST(CurveReflection, SaveLoadRoundTripKeepsShape)
{
    FakeCurveComponent source;
    source.SetShape(MakeCurve({{0.0f, 1.0f}, {1.0f, 3.0f}, {2.0f, 0.5f}}));
    const nlohmann::json out = NS::Object::SerializeComponent(source);

    FakeCurveComponent loaded;
    NS::Object::ApplyJsonFields(loaded, out["fields"]);
    ASSERT_EQ(loaded.Shape().count, 3u);
    EXPECT_TRUE(loaded.Shape() == source.Shape());
}

TEST(CurveReflection, WrongShapeKeepsCurrentValue)
{
    FakeCurveComponent comp;
    const Curve before = MakeCurve({{0.0f, 1.0f}, {1.0f, 3.0f}});
    comp.SetShape(before);

    const nlohmann::json fields = nlohmann::json::parse(R"({"カーブ": {"curve": 5}})");
    NS::Object::ApplyJsonFields(comp, fields);
    EXPECT_TRUE(comp.Shape() == before);

    const nlohmann::json bare = nlohmann::json::parse(R"({"カーブ": 5})");
    NS::Object::ApplyJsonFields(comp, bare);
    EXPECT_TRUE(comp.Shape() == before);
}

TEST(CurveReflection, MalformedPointsAreSkipped)
{
    FakeCurveComponent comp;
    const nlohmann::json fields = nlohmann::json::parse(R"({"カーブ": {"curve": [[1], [2, 3], ["a", 4]]}})");
    NS::Object::ApplyJsonFields(comp, fields);
    ASSERT_EQ(comp.Shape().count, 1u);
    EXPECT_FLOAT_EQ(comp.Shape().keys[0].x, 2.0f);
    EXPECT_FLOAT_EQ(comp.Shape().keys[0].y, 3.0f);
}

TEST(CurveReflection, ExtraPointsBeyondLimitAreDropped)
{
    FakeCurveComponent comp;
    nlohmann::json points = nlohmann::json::array();
    for (int i = 0; i < 9; ++i)
    {
        points.push_back(nlohmann::json{static_cast<float>(i), static_cast<float>(i * 10)});
    }
    nlohmann::json fields;
    fields["カーブ"]["curve"] = points;

    NS::Object::ApplyJsonFields(comp, fields);
    ASSERT_EQ(comp.Shape().count, Curve::k_MaxKeys);
    EXPECT_FLOAT_EQ(comp.Shape().keys[Curve::k_MaxKeys - 1].x, 7.0f);
}

TEST(CurveReflection, LoadSortsKeysAscending)
{
    FakeCurveComponent comp;
    const nlohmann::json fields = nlohmann::json::parse(R"({"カーブ": {"curve": [[2, 20], [0, 0], [1, 10]]}})");
    NS::Object::ApplyJsonFields(comp, fields);
    ASSERT_EQ(comp.Shape().count, 3u);
    EXPECT_FLOAT_EQ(comp.Shape().keys[0].x, 0.0f);
    EXPECT_FLOAT_EQ(comp.Shape().keys[1].x, 1.0f);
    EXPECT_FLOAT_EQ(comp.Shape().keys[2].x, 2.0f);
}

TEST(CurveReflection, EditedCurveDiffersFromDefault)
{
    FakeCurveComponent defaults;
    FakeCurveComponent live;
    const FieldDesc* field = FindField(live.GetReflection(), "カーブ");
    ASSERT_NE(field, nullptr);

    EXPECT_FALSE(NS::Editor::FieldDiffersFromDefault(live, &defaults, *field));

    live.SetShape(MakeCurve({{0.0f, 1.0f}}));
    EXPECT_TRUE(NS::Editor::FieldDiffersFromDefault(live, &defaults, *field));
}

TEST(CurveReflection, RevertRestoresDefaultShape)
{
    FakeCurveComponent defaults;
    FakeCurveComponent live;
    const FieldDesc* field = FindField(live.GetReflection(), "カーブ");
    ASSERT_NE(field, nullptr);

    live.SetShape(MakeCurve({{0.0f, 1.0f}, {1.0f, 3.0f}}));
    NS::Editor::RevertFieldToDefault(live, defaults, *field);
    EXPECT_TRUE(live.Shape() == defaults.Shape());
    EXPECT_FALSE(NS::Editor::FieldDiffersFromDefault(live, &defaults, *field));
}
