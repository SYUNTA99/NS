#include <Game/Level/HitZoneArea.h>
#include <Runtime/Object/Component.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ReflectionJson.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>

#include <gtest/gtest.h>
#include <limits>

namespace
{
    using NS::Game::Level::HitZoneArea;
} // namespace

// 足した直後は気持ちいいの箱。横と縦の半分の幅 0.43、真ん中、威力は減らさない
TEST(HitZoneAreaTest, DefaultsAreACenterBoxOfPoint43)
{
    const HitZoneArea area;
    EXPECT_TRUE(area.IsCenter());
    EXPECT_FALSE(area.IsRound());
    EXPECT_FLOAT_EQ(area.Width(), 0.43f);
    EXPECT_FLOAT_EQ(area.Height(), 0.43f);
    EXPECT_FLOAT_EQ(area.CenterU(), 0.0f);
    EXPECT_FLOAT_EQ(area.CenterV(), 0.0f);
    EXPECT_FLOAT_EQ(area.PowerScale(), 1.0f);
}

// 箱は左右と上下を別々に見る。縁ちょうどは入らない
TEST(HitZoneAreaTest, BoxCoversItsWidthAndHeight)
{
    HitZoneArea area;
    area.SetWidth(0.5f);
    area.SetHeight(0.2f);

    EXPECT_TRUE(area.Contains(0.0f, 0.0f));
    EXPECT_TRUE(area.Contains(0.49f, 0.19f));
    EXPECT_TRUE(area.Contains(-0.49f, -0.19f));
    EXPECT_FALSE(area.Contains(0.5f, 0.0f));
    EXPECT_FALSE(area.Contains(0.0f, 0.2f));
    EXPECT_FALSE(area.Contains(0.0f, -0.3f));
}

// 丸は横と縦の幅を半径にした楕円。箱の角は入らない
TEST(HitZoneAreaTest, RoundCoversAnEllipse)
{
    HitZoneArea area;
    area.SetRound(true);
    area.SetWidth(0.5f);
    area.SetHeight(0.25f);

    EXPECT_TRUE(area.Contains(0.0f, 0.0f));
    EXPECT_TRUE(area.Contains(0.49f, 0.0f));
    EXPECT_TRUE(area.Contains(0.0f, 0.24f));
    // (0.4 / 0.5)² + (0.2 / 0.25)² = 1.28
    EXPECT_FALSE(area.Contains(0.4f, 0.2f));
    EXPECT_FALSE(area.Contains(0.5f, 0.0f));
}

// 位置を動かすと覆う所も動く
TEST(HitZoneAreaTest, CenterMovesTheCoveredPart)
{
    HitZoneArea area;
    area.SetWidth(0.2f);
    area.SetHeight(0.2f);
    area.SetCenterU(0.5f);
    area.SetCenterV(-0.5f);

    EXPECT_FALSE(area.Contains(0.0f, 0.0f));
    EXPECT_TRUE(area.Contains(0.5f, -0.5f));
    EXPECT_TRUE(area.Contains(0.65f, -0.35f));
    EXPECT_FALSE(area.Contains(0.75f, -0.5f));
}

// 広さが 0 の向きがある色はどこも覆わない
TEST(HitZoneAreaTest, ZeroWidthCoversNothing)
{
    HitZoneArea box;
    box.SetWidth(0.0f);
    EXPECT_FALSE(box.Contains(0.0f, 0.0f));

    HitZoneArea round;
    round.SetRound(true);
    round.SetHeight(0.0f);
    EXPECT_FALSE(round.Contains(0.0f, 0.0f));
}

// 広さは 0〜1、位置は -1〜1 へ丸める。威力の倍率は負を 0 にする。非数と無限は 0
TEST(HitZoneAreaTest, FieldsAreClamped)
{
    HitZoneArea area;
    area.SetWidth(1.5f);
    EXPECT_FLOAT_EQ(area.Width(), 1.0f);
    area.SetHeight(-0.5f);
    EXPECT_FLOAT_EQ(area.Height(), 0.0f);
    area.SetCenterU(-2.0f);
    EXPECT_FLOAT_EQ(area.CenterU(), -1.0f);
    area.SetCenterV(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(area.CenterV(), 0.0f);
    area.SetPowerScale(-1.0f);
    EXPECT_FLOAT_EQ(area.PowerScale(), 0.0f);
    area.SetPowerScale(1.5f);
    EXPECT_FLOAT_EQ(area.PowerScale(), 1.5f);
    area.SetPowerScale(std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(area.PowerScale(), 0.0f);
}

// 欄は保存と再読込で残る
TEST(HitZoneAreaTest, FieldsSurviveSaveAndLoad)
{
    NS::Obj::GameObject source;
    HitZoneArea* area = source.AddComponent<HitZoneArea>();
    area->SetCenter(false);
    area->SetRound(true);
    area->SetWidth(0.3f);
    area->SetHeight(0.6f);
    area->SetCenterU(0.2f);
    area->SetCenterV(-0.4f);
    area->SetPowerScale(0.8f);
    const nlohmann::json saved = NS::Obj::SerializeComponent(*area);
    EXPECT_EQ(saved["type"], "HitZoneArea");

    NS::Obj::GameObject destination;
    NS::Obj::Component* created = NS::Obj::CreateComponent("HitZoneArea", destination);
    ASSERT_NE(created, nullptr);
    NS::Obj::ApplyJsonFields(*created, saved["fields"]);
    const HitZoneArea* restored = destination.FindComponent<HitZoneArea>();
    ASSERT_NE(restored, nullptr);
    EXPECT_FALSE(restored->IsCenter());
    EXPECT_TRUE(restored->IsRound());
    EXPECT_FLOAT_EQ(restored->Width(), 0.3f);
    EXPECT_FLOAT_EQ(restored->Height(), 0.6f);
    EXPECT_FLOAT_EQ(restored->CenterU(), 0.2f);
    EXPECT_FLOAT_EQ(restored->CenterV(), -0.4f);
    EXPECT_FLOAT_EQ(restored->PowerScale(), 0.8f);
}
