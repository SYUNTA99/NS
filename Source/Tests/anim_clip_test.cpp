#include "NSlib/CoreObject/Asset/AnimClip.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Core/Windows/Window.h"
#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/Geometry/GltfParser.h"
#include "NSlib/Core/Animation/Retarget.h"
#include "NSlib/Core/Animation/Skeleton.h"
#include "NSlib/Core/Windows/FileSystem.h"
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

// ほかの素材と同じく、区切り文字と . の表記だけ違う名前は 1 つの姿を返す
TEST(AnimClip, NamesThatDifferOnlyInSeparatorsShareOneClip)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AnimClip* slashed = assets.LoadAnimClip("Assets/Models/Xbot.glb#run");
    EXPECT_EQ(slashed, assets.LoadAnimClip("Assets\\Models\\Xbot.glb#run"));
    EXPECT_EQ(slashed, assets.LoadAnimClip("Assets/./Models/Xbot.glb#run"));
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
    EXPECT_EQ(log.ErrorCount(), 4);
}

// スキンのある glb は骨付きの Mesh になり、骨格と骨ごとの影響の球を持つ。当たりは持たない
TEST(MeshAsset, SkinnedFileHoldsTheSkinnedMeshAndItsSkeleton)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "骨付きのメッシュの試し", .size = {64, 64}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};

    const NS::Mesh* mesh = assets.LoadMesh("Assets/Models/Xbot.glb");
    EXPECT_EQ(mesh->Path(), "Assets/Models/Xbot.glb");
    EXPECT_NE(mesh->D3D(), nullptr);
    ASSERT_NE(mesh->Skeleton(), nullptr);
    EXPECT_GT(mesh->Skeleton()->BoneCount(), 0u);
    EXPECT_EQ(mesh->BoneSpheres().size(), mesh->Skeleton()->BoneCount());
    EXPECT_EQ(mesh->Collision(), nullptr);
}

// D3DDevice の無い場では、骨付きの Mesh も描く形を作らずに骨格だけを持ち、ERROR を出さない
TEST(MeshAsset, SkinnedFileWithoutADeviceHoldsOnlyTheSkeletonAndLogsNoError)
{
    ASSERT_EQ(NS::Gfx::Gpu().device, nullptr);
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const TestLogCapture log;
    const NS::Mesh* mesh = assets.LoadMesh("Assets/Models/Xbot.glb");
    EXPECT_EQ(mesh->D3D(), nullptr);
    EXPECT_FALSE(mesh->IsEmpty());
    ASSERT_NE(mesh->Skeleton(), nullptr);
    EXPECT_GT(mesh->Skeleton()->BoneCount(), 0u);
    EXPECT_EQ(log.ErrorCount(), 0);
}

// スキンの無い glb と組み込みの形は、骨格を持たずに当たりを持つ
TEST(MeshAsset, FilesWithoutASkinAndBuiltinShapesHaveNoSkeleton)
{
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    for (const char* name : {"Assets/Models/GoalFlag.glb", "cube"})
    {
        const NS::Mesh* mesh = assets.LoadMesh(name);
        EXPECT_EQ(mesh->Skeleton(), nullptr) << name;
        EXPECT_TRUE(mesh->BoneSpheres().empty()) << name;
        EXPECT_NE(mesh->Collision(), nullptr) << name;
    }
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
