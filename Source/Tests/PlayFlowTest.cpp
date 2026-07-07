#include "GameCore/Level/PlayFlowComponent.h"
#include "GameCore/Level/PlayMode.h"
#include "GameCore/LevelPlayScene.h"

#include <gtest/gtest.h>

#include <utility>

namespace LevelNs = NS::GameCore::Level;

/// Application 依存のない LevelPlayScene を器に、 PlayFlowComponent の進行を player 無しで検証する
/// OnStart は Application::Get() を要求するため呼ばず、 Tick へ dt を直接渡して進める

namespace
{
    constexpr float kDt = 1.0f / 60.0f;

    // 拾得物を PickupComponent で組む。 pickupKind 0=コイン / 1=ゴール
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

TEST(PlayFlow, EnterPlacesPlayStateAtPlayerObject)
{
    LevelPlayScene scene;
    auto& flow = scene.Director().Flow();
    scene.Level().objects.push_back(
        LevelNs::MakePlayerObject(NS::Math::Vector3{7.0f, 2.0f, -4.0f}, NS::Math::Quaternion{}));

    flow.EnterPlay();

    EXPECT_TRUE(flow.PlayModeSub().IsActive());
    EXPECT_NEAR(flow.Play().playerPosition.x, 7.0f, 1e-4f);
    EXPECT_NEAR(flow.Play().playerPosition.y, 2.0f, 1e-4f);
    EXPECT_NEAR(flow.Play().playerPosition.z, -4.0f, 1e-4f);
}

TEST(PlayFlow, FallDeathRestartsLevelOnSameTick)
{
    LevelPlayScene scene;
    auto& flow = scene.Director().Flow();
    // プレイヤー実体を落下死の閾値より下へ置くと、 player 無しでも Tick 1 回で死亡が立つ
    scene.Level().objects.push_back(LevelNs::MakePlayerObject(
        NS::Math::Vector3{0.0f, LevelNs::PlayMode::kFallDeathThreshold - 10.0f, 0.0f}, NS::Math::Quaternion{}));

    flow.EnterPlay();
    // 体力を減らしておくと、 全回復していることが「リスタートで再 Enter が走った」証拠になる
    flow.Play().playerHealth = 3;
    flow.Tick(kDt);

    EXPECT_FALSE(flow.Play().deathTriggered);
    EXPECT_EQ(flow.Play().playerHealth, 8);
}

TEST(PlayFlow, GoalContactSetsClearTriggered)
{
    LevelPlayScene scene;
    auto& flow = scene.Director().Flow();
    scene.Level().objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    // プレイヤー実体と同じ位置にゴールを置くと中心距離 0 で必ず接触する
    scene.Level().objects.push_back(MakePickup(0.0f, 0.0f, 0.0f, 1));

    flow.EnterPlay();
    flow.Tick(kDt);

    EXPECT_TRUE(flow.Play().clearTriggered);
}

TEST(PlayFlow, PausedTickAdvancesNothing)
{
    LevelPlayScene scene;
    auto& flow = scene.Director().Flow();
    scene.Level().objects.push_back(LevelNs::MakePlayerObject(
        NS::Math::Vector3{0.0f, LevelNs::PlayMode::kFallDeathThreshold - 10.0f, 0.0f}, NS::Math::Quaternion{}));

    flow.EnterPlay();
    flow.Play().paused = true;
    flow.Play().playerHealth = 3;
    flow.Tick(kDt);

    // paused 中はルール評価もリスタートも走らない
    EXPECT_FALSE(flow.Play().deathTriggered);
    EXPECT_EQ(flow.Play().playerHealth, 3);
}

TEST(PlayFlow, ExitPlayResetsTransientFlagsAndDeactivates)
{
    LevelPlayScene scene;
    auto& flow = scene.Director().Flow();

    flow.EnterPlay();
    flow.Play().paused = true;
    flow.ExitPlay();

    EXPECT_FALSE(flow.Play().paused);
    EXPECT_FALSE(flow.PlayModeSub().IsActive());
}

TEST(PlayFlow, ReEnterAfterClearResetsFlags)
{
    LevelPlayScene scene;
    auto& flow = scene.Director().Flow();
    scene.Level().objects.push_back(LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{}));
    scene.Level().objects.push_back(MakePickup(0.0f, 0.0f, 0.0f, 1));

    flow.EnterPlay();
    flow.Tick(kDt);
    ASSERT_TRUE(flow.Play().clearTriggered);

    flow.EnterPlay();

    EXPECT_FALSE(flow.Play().clearTriggered);
    EXPECT_FALSE(flow.Play().deathTriggered);
}
