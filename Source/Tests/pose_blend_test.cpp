#include "NSlib/Core/Animation/Skeleton.h"

#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace
{
    // 骨 2 本の姿勢。from は原点で回さず、to は位置・回転・拡大を全部変える
    std::vector<NS::Anim::BonePose> FromPose()
    {
        return {NS::Anim::BonePose{}, NS::Anim::BonePose{}};
    }

    std::vector<NS::Anim::BonePose> ToPose()
    {
        NS::Anim::BonePose moved;
        moved.translation = NS::Vector3(2.0f, 4.0f, -6.0f);
        moved.rotation = NS::Quaternion::CreateFromAxisAngle(NS::Vector3::UnitY, DirectX::XM_PIDIV2);
        moved.scale = NS::Vector3(3.0f, 3.0f, 3.0f);
        return {moved, moved};
    }

    // q と -q は同じ回転なので、どちらの符号でも同じ回転なら等しいとみなす
    void ExpectSameRotation(const NS::Quaternion& actual, const NS::Quaternion& expected)
    {
        EXPECT_NEAR(std::fabs(actual.Dot(expected)), 1.0f, 1e-5f);
    }
} // namespace

// 重み 0 で元の姿勢、1 で次の姿勢になる
TEST(PoseBlendTest, WeightZeroIsFromAndOneIsTo)
{
    const std::vector<NS::Anim::BonePose> from = FromPose();
    const std::vector<NS::Anim::BonePose> to = ToPose();
    std::vector<NS::Anim::BonePose> out;

    NS::Anim::BlendPose(from, to, 0.0f, out);
    ASSERT_EQ(out.size(), from.size());
    EXPECT_EQ(out[1].translation, from[1].translation);
    ExpectSameRotation(out[1].rotation, from[1].rotation);
    EXPECT_EQ(out[1].scale, from[1].scale);

    NS::Anim::BlendPose(from, to, 1.0f, out);
    ASSERT_EQ(out.size(), to.size());
    EXPECT_EQ(out[1].translation, to[1].translation);
    ExpectSameRotation(out[1].rotation, to[1].rotation);
    EXPECT_EQ(out[1].scale, to[1].scale);
}

// 途中の重みでは、位置と拡大は線形、回転は球面の補間の値になる
TEST(PoseBlendTest, MiddleWeightLerpsAndSlerps)
{
    const std::vector<NS::Anim::BonePose> from = FromPose();
    const std::vector<NS::Anim::BonePose> to = ToPose();
    std::vector<NS::Anim::BonePose> out;

    NS::Anim::BlendPose(from, to, 0.25f, out);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].translation, NS::Vector3(0.5f, 1.0f, -1.5f));
    EXPECT_EQ(out[0].scale, NS::Vector3(1.5f, 1.5f, 1.5f));
    // 90 度の 4 分の 1 なので、Y の周りに 22.5 度回っている
    ExpectSameRotation(out[0].rotation,
                       NS::Quaternion::CreateFromAxisAngle(NS::Vector3::UnitY, DirectX::XM_PIDIV2 * 0.25f));
}

// 内積が負の組 (同じ回転の反対の符号) でも、短い方を回る
TEST(PoseBlendTest, NegativeDotTakesTheShortWay)
{
    const NS::Quaternion start = NS::Quaternion::CreateFromAxisAngle(NS::Vector3::UnitY, 0.2f);
    const NS::Quaternion end = NS::Quaternion::CreateFromAxisAngle(NS::Vector3::UnitY, 0.6f);
    const NS::Quaternion flippedEnd{-end.x, -end.y, -end.z, -end.w};
    ASSERT_LT(start.Dot(flippedEnd), 0.0f);

    std::vector<NS::Anim::BonePose> from(1);
    std::vector<NS::Anim::BonePose> to(1);
    from[0].rotation = start;
    to[0].rotation = flippedEnd;
    std::vector<NS::Anim::BonePose> out;

    NS::Anim::BlendPose(from, to, 0.5f, out);
    ASSERT_EQ(out.size(), 1u);
    // 長い方を回ると、間の回転は Y の周りの 0.4 ラジアンから遠く離れる
    ExpectSameRotation(out[0].rotation, NS::Quaternion::CreateFromAxisAngle(NS::Vector3::UnitY, 0.4f));
}

// 骨の数が違う組は混ぜず、ERROR を出して元の姿勢を返す
TEST(PoseBlendTest, MismatchedBoneCountKeepsFrom)
{
    const std::vector<NS::Anim::BonePose> from = FromPose();
    const std::vector<NS::Anim::BonePose> to(3, ToPose()[0]);
    std::vector<NS::Anim::BonePose> out;
    const TestLogCapture log;

    NS::Anim::BlendPose(from, to, 0.5f, out);

    ASSERT_EQ(out.size(), from.size());
    EXPECT_EQ(out[0].translation, from[0].translation);
    EXPECT_EQ(log.ErrorCount(), 1);
}
