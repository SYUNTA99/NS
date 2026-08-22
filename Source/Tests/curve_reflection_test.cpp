#include <Editor/InspectorReflection.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Component.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/Curve.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Reflection/ReflectionJson.h>
#include <gtest/gtest.h>

#include <cstdint>
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

    // モード導入前の Evaluate の写し。直線だけのカーブが従来とビット単位で同値なことの基準にするため
    float LegacyEvaluate(const Curve& curve, float x)
    {
        if (curve.count == 0)
        {
            return 0.0f;
        }
        if (x <= curve.keys[0].x)
        {
            return curve.keys[0].y;
        }
        const std::uint32_t last = curve.count - 1;
        if (x >= curve.keys[last].x)
        {
            return curve.keys[last].y;
        }
        for (std::uint32_t i = 0; i < last; ++i)
        {
            if (x <= curve.keys[i + 1].x)
            {
                const float width = curve.keys[i + 1].x - curve.keys[i].x;
                if (width <= 0.0f)
                {
                    return curve.keys[i + 1].y;
                }
                const float t = (x - curve.keys[i].x) / width;
                return NS::Core::Lerp(curve.keys[i].y, curve.keys[i + 1].y, t);
            }
        }
        return curve.keys[last].y;
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

TEST(CurveReflection, DefaultKeyModeIsLinear)
{
    constexpr Curve::Key key{};
    EXPECT_EQ(key.mode, Curve::InterpMode::Linear);
    EXPECT_FLOAT_EQ(key.inTangent, 0.0f);
    EXPECT_FLOAT_EQ(key.outTangent, 0.0f);
}

TEST(CurveReflection, LinearOnlyEvaluateMatchesLegacyPathBitExact)
{
    const Curve curve = MakeCurve({{0.0f, 0.1f}, {0.7f, 0.33f}, {1.0f, 0.9f}});
    const float samples[] = {0.05f, 0.1f, 0.337f, 0.5f, 0.69f, 0.75f, 0.95f};
    for (const float x : samples)
    {
        // EXPECT_EQ の厳密比較。直線区間が従来の Lerp と同じ経路を通ることの見張りにするため
        EXPECT_EQ(curve.Evaluate(x), LegacyEvaluate(curve, x)) << "x = " << x;
    }
}

TEST(CurveReflection, FlatLinearCurveReturnsExactlyOne)
{
    // MomentumComponent の既定と同じ平ら 2 点。1.0f と厳密一致しないと基準比較のハッシュが崩れるため
    const Curve curve = MakeCurve({{0.0f, 1.0f}, {1.0f, 1.0f}});
    const float samples[] = {0.1f, 0.3f, 0.5f, 0.77f};
    for (const float x : samples)
    {
        EXPECT_EQ(curve.Evaluate(x), 1.0f) << "x = " << x;
    }
}

TEST(CurveReflection, AutoSmoothEasesIntoExtremum)
{
    const Curve curve = MakeCurve({{0.0f, 0.0f, Curve::InterpMode::AutoSmooth},
                                   {0.5f, 1.0f, Curve::InterpMode::AutoSmooth},
                                   {1.0f, 0.0f, Curve::InterpMode::AutoSmooth}});
    // 山の点の自動接線は 0。左端の接線は割線 2 なので x=0.25 の値は 0.125*1 + 0.5 = 0.625 になる
    EXPECT_FLOAT_EQ(curve.Evaluate(0.25f), 0.625f);
}

TEST(CurveReflection, AutoSmoothStaysWithinNeighborYRange)
{
    // 急な区間の隣に平らな区間を置いた形。接線を抑えないと平らな側で隣り合う点の y から飛び出すため
    const Curve curve = MakeCurve({{0.0f, 0.0f, Curve::InterpMode::AutoSmooth},
                                   {0.2f, 0.1f, Curve::InterpMode::AutoSmooth},
                                   {0.5f, 2.0f, Curve::InterpMode::AutoSmooth},
                                   {1.0f, 2.1f, Curve::InterpMode::AutoSmooth}});
    for (std::uint32_t segment = 0; segment + 1 < curve.count; ++segment)
    {
        const float x0 = curve.keys[segment].x;
        const float x1 = curve.keys[segment + 1].x;
        const float yMin = curve.keys[segment].y;
        const float yMax = curve.keys[segment + 1].y;
        for (int step = 0; step <= 100; ++step)
        {
            const float x = x0 + (x1 - x0) * (static_cast<float>(step) / 100.0f);
            const float y = curve.Evaluate(x);
            EXPECT_GE(y, yMin - 1.0e-5f) << "x = " << x;
            EXPECT_LE(y, yMax + 1.0e-5f) << "x = " << x;
        }
    }
}

TEST(CurveReflection, ManualInTangentZeroEntersFlat)
{
    const Curve curve = MakeCurve({{0.0f, 0.0f}, {1.0f, 1.0f, Curve::InterpMode::Manual, 0.0f, 0.0f}});
    // 期待値は -t^3 + t^2 + t から出す。左端が直線で傾き 1、右端の手動接線が 0 になるため
    EXPECT_FLOAT_EQ(curve.Evaluate(0.5f), 0.625f);
    EXPECT_FLOAT_EQ(curve.Evaluate(0.9f), 0.981f);
}

TEST(CurveReflection, ManualOutTangentZeroLeavesFlat)
{
    const Curve curve = MakeCurve({{0.0f, 0.0f, Curve::InterpMode::Manual, 0.0f, 0.0f}, {1.0f, 1.0f}});
    // 期待値は -t^3 + 2t^2 から出す。左端の手動接線が 0、右端が直線で傾き 1 になるため
    EXPECT_FLOAT_EQ(curve.Evaluate(0.5f), 0.375f);
}

TEST(CurveReflection, SortKeysCarriesModeAndTangents)
{
    Curve curve =
        MakeCurve({{2.0f, 20.0f, Curve::InterpMode::Manual, 1.5f, -0.5f}, {0.0f, 0.0f, Curve::InterpMode::AutoSmooth}});
    curve.SortKeys();
    ASSERT_EQ(curve.count, 2u);
    EXPECT_EQ(curve.keys[0].mode, Curve::InterpMode::AutoSmooth);
    EXPECT_EQ(curve.keys[1].mode, Curve::InterpMode::Manual);
    EXPECT_FLOAT_EQ(curve.keys[1].inTangent, 1.5f);
    EXPECT_FLOAT_EQ(curve.keys[1].outTangent, -0.5f);
}

TEST(CurveReflection, SerializeWritesModeTaggedPoints)
{
    FakeCurveComponent comp;
    comp.SetShape(MakeCurve({{0.0f, 1.0f},
                             {0.5f, 2.0f, Curve::InterpMode::AutoSmooth},
                             {1.0f, 3.0f, Curve::InterpMode::Manual, 0.25f, -1.0f}}));

    const nlohmann::json out = NS::Object::SerializeComponent(comp);
    const nlohmann::json& points = out["fields"]["カーブ"]["curve"];
    ASSERT_EQ(points.size(), 3u);
    ASSERT_EQ(points[0].size(), 2u);
    ASSERT_EQ(points[1].size(), 3u);
    EXPECT_EQ(points[1][2].get<int>(), 1);
    ASSERT_EQ(points[2].size(), 5u);
    EXPECT_EQ(points[2][2].get<int>(), 2);
    EXPECT_FLOAT_EQ(points[2][3].get<float>(), 0.25f);
    EXPECT_FLOAT_EQ(points[2][4].get<float>(), -1.0f);
}

TEST(CurveReflection, ModeRoundTripKeepsTangents)
{
    FakeCurveComponent source;
    source.SetShape(MakeCurve({{0.0f, 1.0f},
                               {0.5f, 2.0f, Curve::InterpMode::AutoSmooth},
                               {1.0f, 3.0f, Curve::InterpMode::Manual, 0.25f, -1.0f}}));
    const nlohmann::json out = NS::Object::SerializeComponent(source);

    FakeCurveComponent loaded;
    NS::Object::ApplyJsonFields(loaded, out["fields"]);
    EXPECT_TRUE(loaded.Shape() == source.Shape());
}

TEST(CurveReflection, LegacyTwoElementPointsLoadAsLinear)
{
    FakeCurveComponent comp;
    const nlohmann::json fields = nlohmann::json::parse(R"({"カーブ": {"curve": [[0, 1], [1, 3]]}})");
    NS::Object::ApplyJsonFields(comp, fields);
    ASSERT_EQ(comp.Shape().count, 2u);
    EXPECT_EQ(comp.Shape().keys[0].mode, Curve::InterpMode::Linear);
    EXPECT_EQ(comp.Shape().keys[1].mode, Curve::InterpMode::Linear);
}

TEST(CurveReflection, MismatchedModePointsAreSkipped)
{
    FakeCurveComponent comp;
    // 要素数とモード番号が食い違う点と要素数 4 の点は飛ばす。壊れた点だけ捨てて残りを読む扱いに揃えるため
    const nlohmann::json fields =
        nlohmann::json::parse(R"({"カーブ": {"curve": [[0, 1, 2], [0.25, 4, 1, 9], [0.5, 2, 1, 0, 0], [1, 3, 1]]}})");
    NS::Object::ApplyJsonFields(comp, fields);
    ASSERT_EQ(comp.Shape().count, 1u);
    EXPECT_FLOAT_EQ(comp.Shape().keys[0].x, 1.0f);
    EXPECT_EQ(comp.Shape().keys[0].mode, Curve::InterpMode::AutoSmooth);
}
