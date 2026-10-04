#include "Runtime/Object/Components/CameraModifier.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <limits>
#include <memory>

// カメラのずれ: 位置と注視点を同じだけずらし、向きと地平線は変えない
// 世界の向きのまま押す形 (外れのつんのめり) と、カメラから見た向きへ写す形 (外れの反動の揺れ) がある

namespace
{
    NS::Obj::Curve CurveOf(std::initializer_list<NS::Obj::Curve::Key> keys)
    {
        NS::Obj::Curve curve;
        for (const NS::Obj::Curve::Key& key : keys)
        {
            curve.keys[curve.count] = key;
            ++curve.count;
        }
        return curve;
    }

    NS::Obj::CameraPose PoseOf()
    {
        NS::Obj::CameraPose pose;
        pose.position = NS::Core::Vector3{0.0f, 2.0f, -6.0f};
        pose.target = NS::Core::Vector3{0.0f, 1.0f, 0.0f};
        return pose;
    }

    // 積んだ後の最初の 1 回は進まないので、frame フレーム目まで進めるには frame + 1 回まわす
    void AdvanceTo(NS::Obj::CameraModifier& modifier, int frame)
    {
        for (int i = 0; i <= frame; ++i)
        {
            modifier.Tick();
        }
    }
} // namespace

// 世界の向きのまま、曲線の距離だけ位置と注視点を同じだけずらす
TEST(CameraNudge, WorldNudgeMovesPositionAndTargetByTheCurve)
{
    NS::Obj::CameraNudgeDesc desc;
    desc.direction = NS::Core::Vector3{0.0f, 0.0f, 1.0f};
    desc.distance = CurveOf({{0.0f, 0.2f}, {2.0f, 0.3f}, {8.0f, 0.0f}});
    desc.frames = 9;
    std::unique_ptr<NS::Obj::CameraNudgeModifier> nudge = NS::Obj::CameraNudgeModifier::Create(desc);
    ASSERT_NE(nudge, nullptr);
    const NS::Obj::CameraAxes axes;

    NS::Obj::CameraPose first = PoseOf();
    nudge->Modify(first, axes);
    EXPECT_NEAR(first.position.z, -5.8f, 1.0e-5f);
    EXPECT_NEAR(first.target.z, 0.2f, 1.0e-5f);
    EXPECT_NEAR(first.position.y, 2.0f, 1.0e-5f);

    AdvanceTo(*nudge, 1);
    NS::Obj::CameraPose second = PoseOf();
    nudge->Modify(second, axes);
    EXPECT_NEAR(second.position.z, -5.75f, 1.0e-5f);
    EXPECT_NEAR(second.target.z, 0.25f, 1.0e-5f);
}

// 画面へ写す形は、奥へ向かう分を捨て、画面の上で見える向きだけで曲線の距離ずらす
TEST(CameraNudge, ScreenNudgeUsesOnlyTheDirectionSeenFromTheCamera)
{
    NS::Obj::CameraNudgeDesc desc;
    NS::Core::Vector3 rightAndAway{1.0f, 0.0f, 1.0f};
    rightAndAway.Normalize();
    desc.direction = rightAndAway;
    desc.distance = CurveOf({{0.0f, -0.5f}});
    desc.frames = 4;
    desc.onScreen = true;
    std::unique_ptr<NS::Obj::CameraNudgeModifier> nudge = NS::Obj::CameraNudgeModifier::Create(desc);
    ASSERT_NE(nudge, nullptr);

    NS::Obj::CameraPose pose = PoseOf();
    nudge->Modify(pose, NS::Obj::CameraAxes{});
    // 右へ飛ぶ反動の逆へ、画面の左へ 0.5 m。奥行きは変えない
    EXPECT_NEAR(pose.position.x, -0.5f, 1.0e-5f);
    EXPECT_NEAR(pose.position.z, -6.0f, 1.0e-5f);
    EXPECT_NEAR(pose.target.x, -0.5f, 1.0e-5f);
    EXPECT_NEAR(pose.target.z, 0.0f, 1.0e-5f);
}

// 向きがほぼ真っすぐ画面の奥を指す時は、画面の上の向きが決まらないので動かさない
TEST(CameraNudge, ScreenNudgeHoldsStillWhenTheDirectionPointsIntoTheScreen)
{
    NS::Obj::CameraNudgeDesc desc;
    desc.direction = NS::Core::Vector3{0.0f, 0.0f, 1.0f};
    desc.distance = CurveOf({{0.0f, 0.5f}});
    desc.frames = 4;
    desc.onScreen = true;
    std::unique_ptr<NS::Obj::CameraNudgeModifier> nudge = NS::Obj::CameraNudgeModifier::Create(desc);
    ASSERT_NE(nudge, nullptr);

    NS::Obj::CameraPose pose = PoseOf();
    nudge->Modify(pose, NS::Obj::CameraAxes{});
    EXPECT_TRUE(pose.position == PoseOf().position);
    EXPECT_TRUE(pose.target == PoseOf().target);
}

// フレーム数を描き終えたら終わる。向きの無い・非数の・フレーム数 0 以下の設定は作らない
TEST(CameraNudge, FinishesAfterItsFramesAndRefusesBrokenSettings)
{
    NS::Obj::CameraNudgeDesc desc;
    desc.direction = NS::Core::Vector3{1.0f, 0.0f, 0.0f};
    desc.distance = CurveOf({{0.0f, 0.3f}});
    desc.frames = 3;
    std::unique_ptr<NS::Obj::CameraNudgeModifier> nudge = NS::Obj::CameraNudgeModifier::Create(desc);
    ASSERT_NE(nudge, nullptr);
    AdvanceTo(*nudge, 2);
    EXPECT_FALSE(nudge->IsFinished());
    nudge->Tick();
    EXPECT_TRUE(nudge->IsFinished());

    NS::Obj::CameraNudgeDesc noFrames = desc;
    noFrames.frames = 0;
    EXPECT_EQ(NS::Obj::CameraNudgeModifier::Create(noFrames), nullptr);
    NS::Obj::CameraNudgeDesc noDirection = desc;
    noDirection.direction = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    EXPECT_EQ(NS::Obj::CameraNudgeModifier::Create(noDirection), nullptr);
    NS::Obj::CameraNudgeDesc notANumber = desc;
    notANumber.direction.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(NS::Obj::CameraNudgeModifier::Create(notANumber), nullptr);
    NS::Obj::CameraNudgeDesc brokenCurve = desc;
    brokenCurve.distance.keys[0].y = std::numeric_limits<float>::infinity();
    EXPECT_EQ(NS::Obj::CameraNudgeModifier::Create(brokenCurve), nullptr);
}
