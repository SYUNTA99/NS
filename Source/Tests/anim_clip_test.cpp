#include "NSlib/CoreObject/Asset/AnimClip.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/SkinnedModel.h"
#include "NSlib/Gfx/Renderer.h"
#include "NSlib/Windows/Window.h"
#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/Geometry/GltfParser.h"
#include "NSlib/Core/Animation/Retarget.h"
#include "NSlib/Core/Animation/Skeleton.h"
#include "NSlib/Windows/FileSystem.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// 字面の # の後ろが動きの名前。# が無ければファイルの先頭の 1 本を使う
TEST(AnimClip, LoadsTheNamedClipOrTheFirstOneInTheFile)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AnimClip* run = assets.LoadAnimClip("Assets/Models/Xbot.glb#run");
    ASSERT_NE(run->Clip(), nullptr);
    EXPECT_EQ(run->Clip()->name, "run");
    EXPECT_EQ(run->Path(), "Assets/Models/Xbot.glb#run");
    EXPECT_EQ(run, assets.LoadAnimClip("Assets/Models/Xbot.glb#run"));

    const NS::AnimClip* first = assets.LoadAnimClip("Assets/Models/Xbot.glb");
    ASSERT_NE(first->Clip(), nullptr);
    const NS::AnimClip* named = assets.LoadAnimClip("Assets/Models/Xbot.glb#" + first->Clip()->name);
    ASSERT_NE(named->Clip(), nullptr);
    EXPECT_EQ(first->Clip()->tracks.size(), named->Clip()->tracks.size());
}

// 欄が保存し直す時に字面を失わないよう、どの名前でも字面を持つ。名前は順に、選んでいない・無い動き・読めないファイル・ContentRoot の外
TEST(AnimClip, UnreadableNamesGiveAnEmptyClipWithTheName)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const TestLogCapture log;
    for (const char* name :
         {"", "Assets/Models/Xbot.glb#__ns_missing__", "Assets/Models/__ns_missing__.glb", "../__ns_outside__.glb"})
    {
        const NS::AnimClip* clip = assets.LoadAnimClip(name);
        ASSERT_NE(clip, nullptr) << name;
        EXPECT_EQ(clip->Path(), name);
        EXPECT_EQ(clip->Clip(), nullptr) << name;
        EXPECT_EQ(clip, assets.LoadAnimClip(name)) << name;
    }
    EXPECT_EQ(log.ErrorCount(), 1);
}

TEST(SkinnedModelAsset, UnreadableNamesGiveAnEmptyModelWithTheName)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const TestLogCapture log;
    for (const char* name : {"", "Assets/Models/__ns_missing__.glb", "../__ns_outside__.glb"})
    {
        const NS::SkinnedModel* model = assets.LoadSkinnedModel(name);
        ASSERT_NE(model, nullptr) << name;
        EXPECT_EQ(model->Path(), name);
        EXPECT_EQ(model->GetMesh(), nullptr) << name;
        EXPECT_EQ(model->GetSkeleton(), nullptr) << name;
        EXPECT_EQ(model, assets.LoadSkinnedModel(name)) << name;
    }
    EXPECT_EQ(log.ErrorCount(), 3);
}

TEST(SkinnedModelAsset, ReadableFileHoldsTheSkinnedMeshAndItsSkeleton)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "骨付きのモデルの試し", .size = {64, 64}, .visible = false});
    NS::Gfx::Renderer renderer(NS::Gfx::RendererDesc{}, window);
    ASSERT_TRUE(renderer.IsValid());
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};

    const NS::SkinnedModel* model = assets.LoadSkinnedModel("Assets/Models/Xbot.glb");
    EXPECT_EQ(model->Path(), "Assets/Models/Xbot.glb");
    ASSERT_NE(model->GetMesh(), nullptr);
    EXPECT_NE(model->GetMesh()->D3DSkeletal(), nullptr);
    ASSERT_NE(model->GetSkeleton(), nullptr);
    EXPECT_GT(model->GetSkeleton()->BoneCount(), 0u);
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
    const NS::Anim::AnimClip* run = assets.LoadAnimClip("Assets/Models/Xbot.glb#run")->Clip();
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
