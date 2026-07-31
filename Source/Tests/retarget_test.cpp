#include <gtest/gtest.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/Animation.h>
#include <Runtime/Graphics/Retarget.h>
#include <Runtime/Graphics/Skeleton.h>
#include <Runtime/Math/Math.h>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using NS::Graphics::AnimationClip;
    using NS::Graphics::BindClipsByName;
    using NS::Graphics::Bone;
    using NS::Graphics::BoneTrack;
    using NS::Graphics::Interpolation;
    using NS::Graphics::NormalizeBoneName;
    using NS::Graphics::Skeleton;
    using NS::Math::Quaternion;
    using NS::Math::Vector3;

    Skeleton MakeSkeleton(const std::vector<std::string>& names)
    {
        std::vector<Bone> bones(names.size());
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            bones[i].parentIndex = -1;
            bones[i].name = names[i];
        }
        return Skeleton(std::move(bones));
    }

    Quaternion RotZ(float degrees)
    {
        return Quaternion::CreateFromAxisAngle(Vector3(0.0f, 0.0f, 1.0f), NS::Math::DegreesToRadians(degrees));
    }

    BoneTrack MakeRotationTrack(int boneIndex, float lastTime, float lastDegrees)
    {
        BoneTrack track;
        track.boneIndex = boneIndex;
        track.rotationTimes = {0.0f, lastTime};
        track.rotationValues = {Quaternion::Identity, RotZ(lastDegrees)};
        track.rotationInterp = Interpolation::Linear;
        return track;
    }
} // namespace

class RetargetTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(RetargetTest, NormalizeBoneNameStripsPrefixTrimsAndLowers)
{
    EXPECT_EQ(NormalizeBoneName("mixamorig:LeftArm"), "leftarm");
    EXPECT_EQ(NormalizeBoneName("Armature|Hips"), "hips");
    EXPECT_EQ(NormalizeBoneName("Spine"), "spine");
    EXPECT_EQ(NormalizeBoneName(" Head "), "head");
    EXPECT_EQ(NormalizeBoneName("A:B|C"), "c");
    EXPECT_EQ(NormalizeBoneName(""), "");
}

TEST_F(RetargetTest, BindClipsByNameRemapsMatchingTracksAndDropsUnmatched)
{
    const Skeleton source = MakeSkeleton({"Hips", "Spine", "Head"});
    const Skeleton target = MakeSkeleton({"mixamorig:Spine", "mixamorig:Hips"});

    AnimationClip clip;
    clip.name = "walk";
    clip.duration = 1.0f;
    clip.tracks.push_back(MakeRotationTrack(0, 1.0f, 90.0f));  // Hips
    clip.tracks.push_back(MakeRotationTrack(1, 0.5f, 45.0f));  // Spine
    clip.tracks.push_back(MakeRotationTrack(2, 0.25f, 30.0f)); // Head は target に居ない

    const std::vector<AnimationClip> bound = BindClipsByName({clip}, source, target);

    ASSERT_EQ(bound.size(), 1u);
    EXPECT_EQ(bound[0].name, "walk");
    EXPECT_FLOAT_EQ(bound[0].duration, 1.0f);

    // Head のトラックは落ち、Hips / Spine は target 側の index へ張替わる
    ASSERT_EQ(bound[0].tracks.size(), 2u);
    EXPECT_EQ(bound[0].tracks[0].boneIndex, 1);
    EXPECT_EQ(bound[0].tracks[1].boneIndex, 0);
}

TEST_F(RetargetTest, BindClipsByNameKeepsKeyData)
{
    const Skeleton source = MakeSkeleton({"Hips"});
    const Skeleton target = MakeSkeleton({"mixamorig:Hips"});

    AnimationClip clip;
    clip.name = "walk";
    clip.duration = 2.0f;
    clip.tracks.push_back(MakeRotationTrack(0, 2.0f, 90.0f));

    const std::vector<AnimationClip> bound = BindClipsByName({clip}, source, target);

    ASSERT_EQ(bound.size(), 1u);
    ASSERT_EQ(bound[0].tracks.size(), 1u);
    const BoneTrack& original = clip.tracks[0];
    const BoneTrack& remapped = bound[0].tracks[0];

    ASSERT_EQ(remapped.rotationTimes.size(), original.rotationTimes.size());
    for (std::size_t i = 0; i < original.rotationTimes.size(); ++i)
        EXPECT_FLOAT_EQ(remapped.rotationTimes[i], original.rotationTimes[i]);

    ASSERT_EQ(remapped.rotationValues.size(), original.rotationValues.size());
    for (std::size_t i = 0; i < original.rotationValues.size(); ++i)
    {
        EXPECT_FLOAT_EQ(remapped.rotationValues[i].x, original.rotationValues[i].x);
        EXPECT_FLOAT_EQ(remapped.rotationValues[i].y, original.rotationValues[i].y);
        EXPECT_FLOAT_EQ(remapped.rotationValues[i].z, original.rotationValues[i].z);
        EXPECT_FLOAT_EQ(remapped.rotationValues[i].w, original.rotationValues[i].w);
    }
    EXPECT_EQ(remapped.rotationInterp, original.rotationInterp);
}

TEST_F(RetargetTest, BindClipsByNameDropsClipWithoutAnyMatch)
{
    const Skeleton source = MakeSkeleton({"Hips", "Spine", "Head"});
    const Skeleton target = MakeSkeleton({"mixamorig:Spine", "mixamorig:Hips"});

    AnimationClip walk;
    walk.name = "walk";
    walk.duration = 1.0f;
    walk.tracks.push_back(MakeRotationTrack(0, 1.0f, 90.0f));

    // 全トラックが target に居ない骨だけのクリップ
    AnimationClip nod;
    nod.name = "nod";
    nod.duration = 1.0f;
    nod.tracks.push_back(MakeRotationTrack(2, 1.0f, 15.0f));

    const std::vector<AnimationClip> bound = BindClipsByName({walk, nod}, source, target);

    ASSERT_EQ(bound.size(), 1u);
    EXPECT_EQ(bound[0].name, "walk");
}
