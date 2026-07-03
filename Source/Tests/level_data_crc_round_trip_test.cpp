#include "Game/Level/LevelData.h"
#include "Game/Level/PlayMode.h"
#include "Game/Level/PlayState.h"

#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;

namespace
{
    // 視覚 / 当たりを持たず拾得の意味だけを持つ grid pickup を作る (コイン=0 / ゴール=1)
    LevelNs::ObjectInstance MakePickup(int pickupKind)
    {
        LevelNs::ObjectInstance object{};
        object.flags = LevelNs::kObjectFlagGridAligned;
        LevelNs::ComponentData pickup;
        pickup.typeName = "PickupComponent";
        pickup.fields.push_back(LevelNs::FieldValue{"Pickup Kind", pickupKind});
        object.components.push_back(std::move(pickup));
        return object;
    }
} // namespace

/// Play 中の LevelData 書込禁止保証。 600 tick (10 秒 @60Hz) を回した後の
/// CRC32 が Enter 前と一致することで、 PlayMode 経路で LevelData が変更されないことを
/// runtime にも検証する (compile-time の const& 受取と二段防御)
TEST(PlayModeCrc, RoundTripPreservesLevelData_PMODE_06)
{
    LevelNs::LevelData level;
    level.objects.push_back(
        LevelNs::MakePlayerObject(NS::Math::Vector3{5.0f, 1.0f, -3.0f}, NS::Math::Quaternion{}));
    level.themeId = 7;
    level.coinThreshold = 30;
    level.timeLimitSeconds = 240;
    level.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 0));
    level.objects.push_back(LevelNs::MakeGridObject(1, 0, 0, 1));
    level.objects.push_back(MakePickup(0));
    level.objects.push_back(MakePickup(1));

    const std::uint32_t before = level.ComputeCrc32();

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(level, play);
    for (int i = 0; i < 600; ++i)
        mode.Tick(level, play, 1.0f / 60.0f);
    mode.Exit(play);

    EXPECT_EQ(level.ComputeCrc32(), before) << "PlayMode が LevelData を変更";
}

TEST(PlayModeCrc, RoundTripWithCoinCollectionPreservesLevelData)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    level.objects.push_back(MakePickup(0));
    const std::uint32_t before = level.ComputeCrc32();

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(level, play);
    for (int i = 0; i < 60; ++i)
        mode.Tick(level, play, 1.0f / 60.0f);
    EXPECT_GE(play.coinCount, 1);
    mode.Exit(play);

    EXPECT_EQ(level.ComputeCrc32(), before) << "Coin 取得時に LevelData 変更";
    EXPECT_EQ(level.objects.size(), 2u);
}

TEST(PlayModeCrc, RoundTripWithGoalContactPreservesLevelData)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    level.objects.push_back(MakePickup(1));
    const std::uint32_t before = level.ComputeCrc32();

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(level, play);
    for (int i = 0; i < 60; ++i)
        mode.Tick(level, play, 1.0f / 60.0f);
    EXPECT_TRUE(play.clearTriggered);
    mode.Exit(play);

    EXPECT_EQ(level.ComputeCrc32(), before) << "ゴール 接触時に LevelData 変更";
}
