#include <gtest/gtest.h>

#include <Framework/Core/Filesystem.h>
#include <Framework/Graphics/Animation.h>
#include <Framework/Graphics/GltfLoader.h>
#include <Framework/Graphics/Retarget.h>
#include <Framework/Graphics/Skeleton.h>
#include <Framework/Math/Math.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <span>
#include <vector>

// 実アセット (Khronos サンプル CesiumMan.glb、 人型の歩行) を読み、 skin + animation が取り込めて
// 別時刻で別ポーズになる (実際に動く) ことを確認する。 アセットが無ければ skip
TEST(GltfAnimatedAssetTest, LoadsCesiumManWithSkinAndAnimations)
{
    const std::filesystem::path path = NS::Core::FileSystem::GetExeDirectory() / "Assets" / "Models" / "CesiumMan.glb";
    if (!std::filesystem::exists(path))
    {
        GTEST_SKIP() << "CesiumMan.glb が無い: " << path.string();
    }

    const auto data = NS::Graphics::LoadGltfSkinnedMesh(path.string());
    ASSERT_TRUE(data.IsValid());
    EXPECT_GT(data.vertices.size(), 0u);
    EXPECT_GT(data.indices.size(), 0u);
    EXPECT_GT(data.skeleton.BoneCount(), 0u);
    EXPECT_LE(data.skeleton.BoneCount(), 128u);
    ASSERT_GT(data.animations.size(), 0u);

    std::cout << "[CesiumMan] vertices=" << data.vertices.size() << " indices=" << data.indices.size()
              << " bones=" << data.skeleton.BoneCount() << " animations=" << data.animations.size() << "\n";
    for (const NS::Graphics::AnimationClip& clip : data.animations)
    {
        EXPECT_GT(clip.duration, 0.0f);
        EXPECT_GT(clip.tracks.size(), 0u);
        std::cout << "  clip '" << clip.name << "' duration=" << clip.duration << " tracks=" << clip.tracks.size()
                  << "\n";
    }

    // 先頭クリップを t=0 と中間で評価し、 ポーズが変化している = 実際にアニメする
    const NS::Graphics::AnimationClip& clip = data.animations[0];
    std::vector<NS::Graphics::BonePose> poseStart;
    std::vector<NS::Graphics::BonePose> poseMid;
    NS::Graphics::SampleClipPose(clip, data.skeleton, 0.0f, poseStart);
    NS::Graphics::SampleClipPose(clip, data.skeleton, clip.duration * 0.5f, poseMid);
    ASSERT_EQ(poseStart.size(), data.skeleton.BoneCount());
    ASSERT_EQ(poseMid.size(), data.skeleton.BoneCount());

    bool anyDifference = false;
    for (std::size_t i = 0; i < poseStart.size(); ++i)
    {
        const float rotDot = poseStart[i].rotation.Dot(poseMid[i].rotation);
        const NS::Math::Vector3 posDelta = poseStart[i].translation - poseMid[i].translation;
        if (rotDot < 0.9999f || posDelta.Length() > 1e-4f)
        {
            anyDifference = true;
            break;
        }
    }
    EXPECT_TRUE(anyDifference) << "t=0 と中間でポーズが変化していない";

    // skinned 頂点 AABB の最長軸で向きを判定する。 人型が立っていれば Y (身長) が最長
    // root 上位ノード変換 (アーマチュア Z-up→Y-up) を取りこぼすと Z 最長 = 寝た状態になる
    auto extentOf = [&](const std::vector<NS::Math::Matrix>& palette) {
        const std::span<const NS::Math::Matrix> sp(palette.data(), palette.size());
        NS::Math::Vector3 mn{1e9f, 1e9f, 1e9f};
        NS::Math::Vector3 mx{-1e9f, -1e9f, -1e9f};
        for (const NS::Graphics::SkinnedVertex& v : data.vertices)
        {
            const NS::Math::Vector3 p =
                palette.empty() ? v.position : NS::Graphics::Skeleton::SkinPositionReference(v, sp);
            mn = NS::Math::Vector3::Min(mn, p);
            mx = NS::Math::Vector3::Max(mx, p);
        }
        return NS::Math::Vector3{mx.x - mn.x, mx.y - mn.y, mx.z - mn.z};
    };
    std::vector<NS::Math::Matrix> bindPalette;
    data.skeleton.ComputeBindPalette(bindPalette);
    const NS::Math::Vector3 bindExtent = extentOf(bindPalette);
    std::cout << "[CesiumMan] bind extent x=" << bindExtent.x << " y=" << bindExtent.y << " z=" << bindExtent.z << "\n";

    // 立っている = 身長 (Y) が幅 (X) と奥行 (Z) より大きい
    EXPECT_GT(bindExtent.y, bindExtent.x) << "bind ポーズで Y が最長でない (寝ている可能性)";
    EXPECT_GT(bindExtent.y, bindExtent.z) << "bind ポーズで Y が最長でない (寝ている可能性)";

    // 骨に node 名が入っている (リターゲットの対応づけ鍵)
    std::size_t namedBones = 0;
    for (const NS::Graphics::Bone& bone : data.skeleton.Bones())
        if (!bone.name.empty())
            ++namedBones;
    std::cout << "[CesiumMan] named bones = " << namedBones << " / " << data.skeleton.BoneCount() << "\n";
    EXPECT_EQ(namedBones, data.skeleton.BoneCount()) << "全ボーンに node 名が入っていない";
}

// skin 非依存のソース読込: skin を無視して node 階層＋animation だけから骨格とクリップを取る
TEST(GltfAnimatedAssetTest, LoadsAnimationSourceSkinIndependent)
{
    const std::filesystem::path path = NS::Core::FileSystem::GetExeDirectory() / "Assets" / "Models" / "CesiumMan.glb";
    if (!std::filesystem::exists(path))
    {
        GTEST_SKIP() << "CesiumMan.glb が無い: " << path.string();
    }

    const auto source = NS::Graphics::LoadGltfAnimationSource(path.string());
    ASSERT_TRUE(source.IsValid());
    EXPECT_GT(source.skeleton.BoneCount(), 0u);
    EXPECT_LE(source.skeleton.BoneCount(), 128u);
    EXPECT_GT(source.animations.size(), 0u);

    std::size_t namedBones = 0;
    for (const NS::Graphics::Bone& bone : source.skeleton.Bones())
        if (!bone.name.empty())
            ++namedBones;
    std::cout << "[CesiumMan source] bones=" << source.skeleton.BoneCount() << " named=" << namedBones
              << " animations=" << source.animations.size() << "\n";
    EXPECT_GT(namedBones, 0u) << "ソース骨格に骨名が無い";

    // 失敗系: 存在しないファイルは IsValid()==false
    const auto missing = NS::Graphics::LoadGltfAnimationSource("does_not_exist_xyz.glb");
    EXPECT_FALSE(missing.IsValid());
}

// ファサード: 同一リグの別ファイルアニメを target 骨格へ適用 (第1段)
TEST(GltfAnimatedAssetTest, LoadAnimationsForSkeletonSameRig)
{
    const std::filesystem::path path = NS::Core::FileSystem::GetExeDirectory() / "Assets" / "Models" / "CesiumMan.glb";
    if (!std::filesystem::exists(path))
    {
        GTEST_SKIP() << "CesiumMan.glb が無い: " << path.string();
    }

    const auto skinned = NS::Graphics::LoadGltfSkinnedMesh(path.string());
    ASSERT_TRUE(skinned.IsValid());

    const auto clips = NS::Graphics::LoadAnimationsForSkeleton(path.string(), skinned.skeleton);
    ASSERT_FALSE(clips.empty());
    for (const NS::Graphics::AnimationClip& clip : clips)
    {
        EXPECT_TRUE(clip.IsValid());
        for (const NS::Graphics::BoneTrack& track : clip.tracks)
        {
            EXPECT_GE(track.boneIndex, 0);
            EXPECT_LT(track.boneIndex, static_cast<int>(skinned.skeleton.BoneCount()));
        }
    }
    std::cout << "[CesiumMan] LoadAnimationsForSkeleton clips=" << clips.size() << "\n";
}

// 恒等性: 同一リグなら facade 適用クリップが直読みクリップと同じポーズを出す (第1段の正しさ)
TEST(GltfAnimatedAssetTest, SameRigBindMatchesDirectLoad)
{
    const std::filesystem::path path = NS::Core::FileSystem::GetExeDirectory() / "Assets" / "Models" / "CesiumMan.glb";
    if (!std::filesystem::exists(path))
    {
        GTEST_SKIP() << "CesiumMan.glb が無い: " << path.string();
    }

    const auto skinned = NS::Graphics::LoadGltfSkinnedMesh(path.string());
    ASSERT_TRUE(skinned.IsValid());
    ASSERT_FALSE(skinned.animations.empty());
    const auto bound = NS::Graphics::LoadAnimationsForSkeleton(path.string(), skinned.skeleton);
    ASSERT_FALSE(bound.empty());

    const NS::Graphics::AnimationClip& direct = skinned.animations[0];
    const NS::Graphics::AnimationClip& rebound = bound[0];
    const float duration = direct.duration;

    std::vector<NS::Graphics::BonePose> poseDirect;
    std::vector<NS::Graphics::BonePose> poseRebound;
    float maxRotErr = 0.0f;
    float maxPosErr = 0.0f;
    for (const float frac : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        const float t = duration * frac;
        NS::Graphics::SampleClipPose(direct, skinned.skeleton, t, poseDirect);
        NS::Graphics::SampleClipPose(rebound, skinned.skeleton, t, poseRebound);
        ASSERT_EQ(poseDirect.size(), poseRebound.size());
        for (std::size_t i = 0; i < poseDirect.size(); ++i)
        {
            const float dot = std::fabs(poseDirect[i].rotation.Dot(poseRebound[i].rotation));
            maxRotErr = std::max(maxRotErr, 1.0f - dot);
            maxPosErr = std::max(maxPosErr, (poseDirect[i].translation - poseRebound[i].translation).Length());
        }
    }
    std::cout << "[CesiumMan] same-rig identity maxRotErr=" << maxRotErr << " maxPosErr=" << maxPosErr << "\n";
    EXPECT_LT(maxRotErr, 1e-3f) << "同一リグなのに直読みとポーズが一致しない (回転)";
    EXPECT_LT(maxPosErr, 1e-3f) << "同一リグなのに直読みとポーズが一致しない (並進)";
}
