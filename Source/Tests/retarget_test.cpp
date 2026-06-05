#include <gtest/gtest.h>

#include <Framework/Graphics/Retarget.h>

#include <set>
#include <utility>
#include <vector>

TEST(RetargetTest, NormalizeBoneNameStripsNamespaceAndLowercases)
{
    EXPECT_EQ(NS::Graphics::NormalizeBoneName("mixamorig:Hips"), "hips");
    EXPECT_EQ(NS::Graphics::NormalizeBoneName("Armature|mixamorig:LeftArm"), "leftarm");
    EXPECT_EQ(NS::Graphics::NormalizeBoneName("Armature|Spine"), "spine");
    EXPECT_EQ(NS::Graphics::NormalizeBoneName("  Head  "), "head");
    EXPECT_EQ(NS::Graphics::NormalizeBoneName("Hips"), "hips");
    EXPECT_EQ(NS::Graphics::NormalizeBoneName(""), "");
    EXPECT_EQ(NS::Graphics::NormalizeBoneName("   "), "");
}

namespace
{
    NS::Graphics::BoneTrack MakeRotTrack(int boneIndex)
    {
        NS::Graphics::BoneTrack track;
        track.boneIndex = boneIndex;
        track.rotationTimes = {0.0f, 1.0f};
        track.rotationValues = {NS::Math::Quaternion::Identity, NS::Math::Quaternion::Identity};
        return track;
    }
} // namespace

TEST(RetargetTest, BindClipsByNameReindexesByBoneName)
{
    std::vector<NS::Graphics::Bone> sourceBones(2);
    sourceBones[0].name = "mixamorig:Hips";
    sourceBones[1].name = "mixamorig:Spine";
    sourceBones[1].parentIndex = 0;
    const NS::Graphics::Skeleton source(std::move(sourceBones));

    // 名前も並びも違う target (Spine→1, Hips→2, Root は未対応)
    std::vector<NS::Graphics::Bone> targetBones(3);
    targetBones[0].name = "Root";
    targetBones[1].name = "Spine";
    targetBones[2].name = "Hips";
    const NS::Graphics::Skeleton target(std::move(targetBones));

    NS::Graphics::AnimationClip clip;
    clip.name = "walk";
    clip.duration = 1.0f;
    clip.tracks = {MakeRotTrack(0), MakeRotTrack(1)};

    const std::vector<NS::Graphics::AnimationClip> bound = NS::Graphics::BindClipsByName(source, {clip}, target);
    ASSERT_EQ(bound.size(), 1u);
    std::set<int> indices;
    for (const NS::Graphics::BoneTrack& track : bound[0].tracks)
        indices.insert(track.boneIndex);
    EXPECT_EQ(indices, (std::set<int>{1, 2})); // Hips→2, Spine→1 へ張替え
}

TEST(RetargetTest, BindClipsByNameDropsUnmatchedClips)
{
    std::vector<NS::Graphics::Bone> sourceBones(1);
    sourceBones[0].name = "Nonexistent";
    const NS::Graphics::Skeleton source(std::move(sourceBones));
    std::vector<NS::Graphics::Bone> targetBones(1);
    targetBones[0].name = "Hips";
    const NS::Graphics::Skeleton target(std::move(targetBones));

    NS::Graphics::AnimationClip clip;
    clip.name = "x";
    clip.duration = 1.0f;
    clip.tracks = {MakeRotTrack(0)};

    const std::vector<NS::Graphics::AnimationClip> bound = NS::Graphics::BindClipsByName(source, {clip}, target);
    EXPECT_TRUE(bound.empty()); // 一致 0 のクリップは除外
}
