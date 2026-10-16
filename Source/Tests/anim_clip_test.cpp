#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/Geometry/GltfParser.h"
#include "NSlib/Core/Animation/Retarget.h"
#include "NSlib/Core/Animation/Skeleton.h"
#include "NSlib/Windows/FileSystem.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// 字面の # の後ろが動きの名前。# が無ければファイルの先頭の 1 本を使う
TEST(AnimClip, LoadsTheNamedClipOrTheFirstOneInTheFile)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::Anim::AnimClip* run = assets.LoadAnimClip("Assets/Models/Xbot.glb#run");
    ASSERT_NE(run, nullptr);
    EXPECT_EQ(run->name, "run");

    const NS::Anim::AnimClip* first = assets.LoadAnimClip("Assets/Models/Xbot.glb");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, assets.LoadAnimClip("Assets/Models/Xbot.glb#" + first->name));

    EXPECT_EQ(assets.LoadAnimClip("Assets/Models/Xbot.glb#__ns_missing__"), nullptr);
    EXPECT_EQ(assets.LoadAnimClip(""), nullptr);
}

// トラックは骨を名前で持つので、どの骨格でも名前で番号を引ける。無い骨は -1
TEST(AnimClip, TracksFindBonesByNameInTheSkeletonThatPlaysThem)
{
    const NS::Anim::Skeleton skeleton{{{.parentIndex = -1, .name = "mixamorig:Hips"}, {.parentIndex = 0, .name = "Spine"}}};
    NS::Anim::AnimClip clip;
    clip.tracks.resize(3);
    clip.tracks[0].boneName = "spine";
    clip.tracks[1].boneName = "Armature|Hips";
    clip.tracks[2].boneName = "Head";
    EXPECT_EQ(NS::Anim::FindTrackBones(clip, skeleton), (std::vector<int>{1, 0, -1}));
}

// 読んだクリップのトラックは、同じファイルの骨格の骨を名前で指している
TEST(AnimClip, LoadedTracksNameTheBonesOfTheirModel)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::Anim::AnimClip* run = assets.LoadAnimClip("Assets/Models/Xbot.glb#run");
    ASSERT_NE(run, nullptr);
    ASSERT_FALSE(run->tracks.empty());
    const NS::AnimSource source = NS::LoadGltfAnimationSource(NS::OS::FileSystem::Combine(
        NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Models"), "Xbot.glb"));
    ASSERT_TRUE(source.IsValid());
    for (const int bone : NS::Anim::FindTrackBones(*run, source.skeleton))
    {
        EXPECT_GE(bone, 0);
    }
}
