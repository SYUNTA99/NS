#include "Game/Level/LevelData.h"
#include "Game/Level/PlayMode.h"
#include "Game/Level/PlayState.h"

#include <gtest/gtest.h>

#include <utility>

namespace LevelNs = NS::Game::Level;

namespace
{
    // 拾得物を PickupComponent で組む。 pickupKind 0=コイン / 1=ゴールゴール
    LevelNs::ObjectInstance MakePickup(float x, float y, float z, int pickupKind)
    {
        LevelNs::ObjectInstance object;
        object.positionX = x;
        object.positionY = y;
        object.positionZ = z;
        LevelNs::ComponentData pickup;
        pickup.typeName = "PickupComponent";
        pickup.fields.push_back(LevelNs::FieldValue{"Pickup Kind", pickupKind});
        object.components.push_back(std::move(pickup));
        return object;
    }
} // namespace

TEST(PlayMode, EnterInitializesPlayerAtPlayerObject)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{5.0f, 2.0f, 3.0f}, NS::Math::Quaternion{}));
    LevelNs::PlayState play;
    LevelNs::PlayMode mode;

    mode.Enter(lv, play);

    EXPECT_NEAR(play.playerPosition.x, 5.0f, 1e-4f);
    // プレイヤー実体の位置は capsule 中心 world 位置そのものなので player はその座標へ正確に置かれる
    EXPECT_NEAR(play.playerPosition.y, 2.0f, 1e-4f);
    EXPECT_NEAR(play.playerPosition.z, 3.0f, 1e-4f);
    EXPECT_EQ(play.coinCount, 0);
    EXPECT_FALSE(play.paused);
    EXPECT_FALSE(play.deathTriggered);
    EXPECT_FALSE(play.clearTriggered);
}

TEST(PlayMode, PausedTickDoesNotEvaluateRules)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    // プレイヤー実体と同じ位置に coin を置くと中心距離が近く、 非 paused なら取得される位置
    lv.objects.push_back(MakePickup(0.0f, 0.0f, 0.0f, 0));

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(lv, play);
    play.paused = true;
    mode.Tick(lv, play, 1.0f / 60.0f);

    // paused 中はルール (coin 取得 / 落下死) を一切評価しない
    EXPECT_EQ(play.coinCount, 0);
}

TEST(PlayMode, FallDeathTriggersWhenBelowThreshold)
{
    LevelNs::LevelData lv;
    LevelNs::PlayState play;
    LevelNs::PlayMode mode;

    mode.Enter(lv, play);
    // 物理は CMC が担うため、 PlayMode は Transform からミラーされた playerPosition を読むだけ
    // 閾値より下へ置いて Tick すると落下死が立つ
    play.playerPosition.y = LevelNs::PlayMode::kFallDeathThreshold - 1.0f;
    mode.Tick(lv, play, 1.0f / 60.0f);

    EXPECT_TRUE(play.deathTriggered);
}

TEST(PlayMode, CoinContactIncrementsCounter)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    // プレイヤー実体と同じ位置に coin を置くと中心距離 0 で必ず pickup
    lv.objects.push_back(MakePickup(0.0f, 0.0f, 0.0f, 0));

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(lv, play);
    mode.Tick(lv, play, 1.0f / 60.0f);

    EXPECT_GE(play.coinCount, 1);

    const auto c1 = play.coinCount;
    mode.Tick(lv, play, 1.0f / 60.0f);
    EXPECT_EQ(play.coinCount, c1);
}

TEST(PlayMode, PowerStarTriggersClear)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    lv.objects.push_back(MakePickup(0.0f, 0.0f, 0.0f, 1));

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(lv, play);
    mode.Tick(lv, play, 1.0f / 60.0f);

    EXPECT_TRUE(play.clearTriggered);
}

TEST(PlayMode, PickupComponentCoinIncrementsCounter)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));

    // 拾得は PickupComponent が駆動する (種別フィールドではなく component が表す)
    LevelNs::ObjectInstance coin;
    coin.positionX = 0.0f;
    coin.positionY = 0.0f;
    coin.positionZ = 0.0f;
    LevelNs::ComponentData pickup;
    pickup.typeName = "PickupComponent";
    pickup.fields.push_back(LevelNs::FieldValue{"Pickup Kind", 0});
    coin.components.push_back(std::move(pickup));
    lv.objects.push_back(std::move(coin));

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(lv, play);
    mode.Tick(lv, play, 1.0f / 60.0f);

    EXPECT_GE(play.coinCount, 1);
}

TEST(PlayMode, PickupComponentStarTriggersClear)
{
    LevelNs::LevelData lv;
    lv.objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));

    LevelNs::ObjectInstance goal;
    goal.positionX = 0.0f;
    goal.positionY = 0.0f;
    goal.positionZ = 0.0f;
    LevelNs::ComponentData pickup;
    pickup.typeName = "PickupComponent";
    pickup.fields.push_back(LevelNs::FieldValue{"Pickup Kind", 1});
    goal.components.push_back(std::move(pickup));
    lv.objects.push_back(std::move(goal));

    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(lv, play);
    mode.Tick(lv, play, 1.0f / 60.0f);

    EXPECT_TRUE(play.clearTriggered);
}

TEST(PlayMode, ExitResetsTransientFlags)
{
    LevelNs::LevelData lv;
    LevelNs::PlayState play;
    LevelNs::PlayMode mode;
    mode.Enter(lv, play);
    play.paused = true;
    play.clearTriggered = true;
    play.deathTriggered = true;

    mode.Exit(play);

    EXPECT_FALSE(play.paused);
    EXPECT_FALSE(play.clearTriggered);
    EXPECT_FALSE(play.deathTriggered);
}
