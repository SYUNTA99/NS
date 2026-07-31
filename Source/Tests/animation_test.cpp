#include <gtest/gtest.h>
#include <Runtime/Graphics/Animation.h>
#include <Runtime/Graphics/Skeleton.h>
#include <Runtime/Math/Math.h>
#include <vector>

namespace
{
    using NS::Graphics::AnimationClip;
    using NS::Graphics::Bone;
    using NS::Graphics::BonePose;
    using NS::Graphics::BoneTrack;
    using NS::Graphics::Interpolation;
    using NS::Graphics::SampleClipPose;
    using NS::Graphics::SampleQuat;
    using NS::Graphics::SampleVec3;
    using NS::Graphics::Skeleton;
    using NS::Math::Quaternion;
    using NS::Math::Vector3;

    constexpr float k_Eps = 1e-4f;

    void ExpectVec3Near(const Vector3& actual, const Vector3& expected, float eps = k_Eps)
    {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }

    Quaternion RotZ(float degrees)
    {
        return Quaternion::CreateFromAxisAngle(Vector3(0.0f, 0.0f, 1.0f), NS::Math::DegreesToRadians(degrees));
    }
} // namespace

TEST(AnimationSampleTest, Vec3LinearMidpoint)
{
    const std::vector<float> times{0.0f, 1.0f};
    const std::vector<Vector3> values{Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 0.0f, 0.0f)};
    const Vector3 r = SampleVec3(times, values, Interpolation::Linear, 0.25f, Vector3(0, 0, 0));
    ExpectVec3Near(r, Vector3(2.5f, 0.0f, 0.0f));
}

TEST(AnimationSampleTest, Vec3ClampsOutOfRange)
{
    const std::vector<float> times{1.0f, 2.0f};
    const std::vector<Vector3> values{Vector3(1.0f, 0.0f, 0.0f), Vector3(2.0f, 0.0f, 0.0f)};
    ExpectVec3Near(SampleVec3(times, values, Interpolation::Linear, 0.0f, Vector3(9, 9, 9)), Vector3(1, 0, 0));
    ExpectVec3Near(SampleVec3(times, values, Interpolation::Linear, 5.0f, Vector3(9, 9, 9)), Vector3(2, 0, 0));
}

TEST(AnimationSampleTest, Vec3StepHoldsLeftKey)
{
    const std::vector<float> times{0.0f, 1.0f};
    const std::vector<Vector3> values{Vector3(1.0f, 0.0f, 0.0f), Vector3(2.0f, 0.0f, 0.0f)};
    ExpectVec3Near(SampleVec3(times, values, Interpolation::Step, 0.7f, Vector3(0, 0, 0)), Vector3(1, 0, 0));
}

TEST(AnimationSampleTest, Vec3EmptyReturnsFallback)
{
    const std::vector<float> times;
    const std::vector<Vector3> values;
    ExpectVec3Near(SampleVec3(times, values, Interpolation::Linear, 0.5f, Vector3(7, 8, 9)), Vector3(7, 8, 9));
}

TEST(AnimationSampleTest, QuatSlerpMidpointIs45Degrees)
{
    const std::vector<float> times{0.0f, 1.0f};
    const std::vector<Quaternion> values{Quaternion::Identity, RotZ(90.0f)};
    const Quaternion q = SampleQuat(times, values, Interpolation::Linear, 0.5f, Quaternion::Identity);
    // 45°Z 回転で (1,0,0) -> (cos45, sin45, 0)
    const Vector3 r = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), q);
    ExpectVec3Near(r, Vector3(0.70710678f, 0.70710678f, 0.0f));
}

TEST(AnimationSampleTest, QuatSlerpTakesShortestPath)
{
    // 終端を同じ回転の遠い表現へ負反転しても最短経路で 45°Z になる
    const Quaternion rot90 = RotZ(90.0f);
    const Quaternion negated{-rot90.x, -rot90.y, -rot90.z, -rot90.w};
    const std::vector<float> times{0.0f, 1.0f};
    const std::vector<Quaternion> values{Quaternion::Identity, negated};
    const Quaternion q = SampleQuat(times, values, Interpolation::Linear, 0.5f, Quaternion::Identity);
    const Vector3 r = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), q);
    ExpectVec3Near(r, Vector3(0.70710678f, 0.70710678f, 0.0f));
}

TEST(AnimationSampleTest, QuatStepHoldsLeftKey)
{
    const std::vector<float> times{0.0f, 1.0f};
    const std::vector<Quaternion> values{Quaternion::Identity, RotZ(90.0f)};
    const Quaternion q = SampleQuat(times, values, Interpolation::Step, 0.9f, Quaternion::Identity);
    const Vector3 r = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), q);
    ExpectVec3Near(r, Vector3(1.0f, 0.0f, 0.0f)); // 左キー=identity 保持
}

TEST(AnimationClipTest, SampleClipPoseAnimatesTrackedBoneAndKeepsBindForOthers)
{
    std::vector<Bone> bones(2);
    bones[0].parentIndex = -1;
    bones[0].bindLocal.translation = Vector3(5.0f, 0.0f, 0.0f);
    bones[1].parentIndex = 0;
    bones[1].bindLocal.translation = Vector3(0.0f, 3.0f, 0.0f);
    Skeleton skeleton(std::move(bones));

    AnimationClip clip;
    clip.name = "spin";
    clip.duration = 1.0f;
    BoneTrack track;
    track.boneIndex = 1;
    track.rotationTimes = {0.0f, 1.0f};
    track.rotationValues = {Quaternion::Identity, RotZ(90.0f)};
    track.rotationInterp = Interpolation::Linear;
    clip.tracks.push_back(track);

    std::vector<BonePose> pose;
    SampleClipPose(clip, skeleton, 0.5f, pose);
    ASSERT_EQ(pose.size(), 2u);

    // bone0 はトラック無し -> bindLocal 据え置き
    ExpectVec3Near(pose[0].translation, Vector3(5.0f, 0.0f, 0.0f));

    // bone1 は回転のみ動く -> translation は bindLocal、 rotation は 45°Z
    ExpectVec3Near(pose[1].translation, Vector3(0.0f, 3.0f, 0.0f));
    const Vector3 r = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), pose[1].rotation);
    ExpectVec3Near(r, Vector3(0.70710678f, 0.70710678f, 0.0f));
}
