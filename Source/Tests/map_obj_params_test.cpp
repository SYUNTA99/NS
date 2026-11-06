#include "Game/Level/LaunchArc.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/MapObjParams.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"

#include <gtest/gtest.h>

// 質量は正の数だけを書く。0・負・無限大を読むと今の値を残す。Mass() の読み替えでなく欄の値で見る
TEST(MapObjParams, MassFieldKeepsTheValueOnZeroNegativeAndInfinite)
{
    GL::Level::MapObjParams params;
    const NS::FieldDesc* field = NS::FindField(params.GetReflection(), "質量");
    ASSERT_NE(field, nullptr);
    float before = 0.0f;
    field->get(&params, &before);
    // 1e39 は float に収まらず、読むと無限大になる
    for (const double value : {0.0, -2.0, 1.0e39})
    {
        SCOPED_TRACE(value);
        EXPECT_EQ(NS::ApplyJsonFields(&params, nlohmann::json{{"質量", value}}), 0u);
        float after = 0.0f;
        field->get(&params, &after);
        EXPECT_EQ(after, before);
    }
}

// 飛び方の欄の表示名と既定。表示名は保存の鍵になる
TEST(MapObjParams, LaunchShapeDefaultsBelongToParams)
{
    GL::Level::MapObjParams params;
    const nlohmann::json fields = NS::SerializeClassObjFields(&params);
    const nlohmann::json expected = {{"押し飛ばしの高さ", 2.0f},
                                     {"押し飛ばしの上昇重力", 25.0f},
                                     {"下りの速さの倍率", 1.4f},
                                     {"頂点の帯の縦速度", 1.0f},
                                     {"頂点の帯の重力倍率", 0.5f},
                                     {"外れで飛ばす相手の弧の高さの割合", 0.35f},
                                     {"外れで飛ばす相手の距離の割合", 0.1f}};
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
    }
    const GL::Level::LaunchShape shape = params.Launch();
    EXPECT_FLOAT_EQ(shape.apexHeight, 2.0f);
    EXPECT_FLOAT_EQ(shape.riseGravity, 25.0f);
    EXPECT_FLOAT_EQ(shape.fallGravityScale, 1.4f);
    EXPECT_FLOAT_EQ(shape.apexBandSpeed, 1.0f);
    EXPECT_FLOAT_EQ(shape.apexBandGravityScale, 0.5f);
    EXPECT_FLOAT_EQ(shape.missHeightRatio, 0.35f);
    EXPECT_FLOAT_EQ(shape.missDistanceRatio, 0.1f);
}

// 着地の発生位置と消去の時機はGameが持つ
TEST(MapObjParams, LaunchSynchronizationDefaultsBelongToLaunchEffects)
{
    GL::Level::LaunchEffects effects;
    const nlohmann::json fields = NS::SerializeClassObjFields(&effects);
    const nlohmann::json expected = {{"着地の粉の寿命フレーム", 24}, {"着地の粉を浮かせる高さ", 0.3f}};
    GL::Level::MapObjParams params;
    const nlohmann::json paramFields = NS::SerializeClassObjFields(&params);
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
        EXPECT_FALSE(paramFields.contains(it.key())) << it.key();
    }
}
