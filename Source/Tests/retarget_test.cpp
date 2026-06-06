#include <gtest/gtest.h>

#include <Framework/Graphics/Retarget.h>
#include <Framework/Graphics/Skeleton.h>
#include <Framework/Math/Math.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
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

TEST(RetargetTest, GuessHumanoidBoneMixamoNames)
{
    using NS::Graphics::GuessHumanoidBone;
    using HB = NS::Graphics::HumanoidBone;
    EXPECT_EQ(GuessHumanoidBone("hips"), HB::Hips);
    EXPECT_EQ(GuessHumanoidBone("spine"), HB::Spine);
    EXPECT_EQ(GuessHumanoidBone("spine2"), HB::Chest);
    EXPECT_EQ(GuessHumanoidBone("neck"), HB::Neck);
    EXPECT_EQ(GuessHumanoidBone("head"), HB::Head);
    EXPECT_EQ(GuessHumanoidBone("leftshoulder"), HB::LeftShoulder);
    EXPECT_EQ(GuessHumanoidBone("leftarm"), HB::LeftUpperArm);
    EXPECT_EQ(GuessHumanoidBone("leftforearm"), HB::LeftLowerArm);
    EXPECT_EQ(GuessHumanoidBone("lefthand"), HB::LeftHand);
    EXPECT_EQ(GuessHumanoidBone("leftupleg"), HB::LeftUpperLeg);
    EXPECT_EQ(GuessHumanoidBone("leftleg"), HB::LeftLowerLeg);
    EXPECT_EQ(GuessHumanoidBone("leftfoot"), HB::LeftFoot);
    EXPECT_EQ(GuessHumanoidBone("rightarm"), HB::RightUpperArm);
    EXPECT_EQ(GuessHumanoidBone("rightforearm"), HB::RightLowerArm);
    EXPECT_EQ(GuessHumanoidBone("rightupleg"), HB::RightUpperLeg);
    EXPECT_EQ(GuessHumanoidBone("rightfoot"), HB::RightFoot);
}

TEST(RetargetTest, GuessHumanoidBoneVrmAndUeNames)
{
    using NS::Graphics::GuessHumanoidBone;
    using HB = NS::Graphics::HumanoidBone;
    // 上腕/前腕を語で明示する命名
    EXPECT_EQ(GuessHumanoidBone("leftupperarm"), HB::LeftUpperArm);
    EXPECT_EQ(GuessHumanoidBone("leftlowerarm"), HB::LeftLowerArm);
    EXPECT_EQ(GuessHumanoidBone("leftupperleg"), HB::LeftUpperLeg);
    EXPECT_EQ(GuessHumanoidBone("leftlowerleg"), HB::LeftLowerLeg);
    EXPECT_EQ(GuessHumanoidBone("chest"), HB::Chest);
    // _l / _r 接尾辞で左右を表す命名
    EXPECT_EQ(GuessHumanoidBone("pelvis"), HB::Hips);
    EXPECT_EQ(GuessHumanoidBone("upperarm_l"), HB::LeftUpperArm);
    EXPECT_EQ(GuessHumanoidBone("lowerarm_r"), HB::RightLowerArm);
    EXPECT_EQ(GuessHumanoidBone("hand_l"), HB::LeftHand);
    EXPECT_EQ(GuessHumanoidBone("clavicle_l"), HB::LeftShoulder);
    EXPECT_EQ(GuessHumanoidBone("thigh_l"), HB::LeftUpperLeg);
    EXPECT_EQ(GuessHumanoidBone("calf_r"), HB::RightLowerLeg);
    EXPECT_EQ(GuessHumanoidBone("foot_l"), HB::LeftFoot);
}

TEST(RetargetTest, GuessHumanoidBoneUnknownReturnsCount)
{
    EXPECT_EQ(NS::Graphics::GuessHumanoidBone("randomthing"), NS::Graphics::HumanoidBone::Count);
    EXPECT_EQ(NS::Graphics::GuessHumanoidBone(""), NS::Graphics::HumanoidBone::Count);
}

TEST(RetargetTest, BuildHumanoidMapAssignsKnownBones)
{
    std::vector<NS::Graphics::Bone> bones(5);
    bones[0].name = "mixamorig:Hips";
    bones[1].name = "mixamorig:Spine";
    bones[1].parentIndex = 0;
    bones[2].name = "mixamorig:LeftArm";
    bones[2].parentIndex = 1;
    bones[3].name = "mixamorig:LeftForeArm";
    bones[3].parentIndex = 2;
    bones[4].name = "mixamorig:UnknownThing";
    bones[4].parentIndex = 0;
    const NS::Graphics::Skeleton skel(std::move(bones));

    const NS::Graphics::HumanoidMap map = NS::Graphics::BuildHumanoidMap(skel);
    auto slot = [&](NS::Graphics::HumanoidBone hb) { return map.boneIndex[static_cast<std::size_t>(hb)]; };
    EXPECT_EQ(slot(NS::Graphics::HumanoidBone::Hips), 0);
    EXPECT_EQ(slot(NS::Graphics::HumanoidBone::Spine), 1);
    EXPECT_EQ(slot(NS::Graphics::HumanoidBone::LeftUpperArm), 2);
    EXPECT_EQ(slot(NS::Graphics::HumanoidBone::LeftLowerArm), 3);
    EXPECT_EQ(slot(NS::Graphics::HumanoidBone::Head), -1); // 無い骨は -1
}

TEST(RetargetTest, BuildHumanoidMapFirstBoneWinsOnDuplicate)
{
    std::vector<NS::Graphics::Bone> bones(3);
    bones[0].name = "Spine";
    bones[1].name = "Spine1";
    bones[1].parentIndex = 0;
    bones[2].name = "Spine2";
    bones[2].parentIndex = 1;
    const NS::Graphics::Skeleton skel(std::move(bones));

    const NS::Graphics::HumanoidMap map = NS::Graphics::BuildHumanoidMap(skel);
    EXPECT_EQ(map.boneIndex[static_cast<std::size_t>(NS::Graphics::HumanoidBone::Spine)], 0); // 親側優先
    EXPECT_EQ(map.boneIndex[static_cast<std::size_t>(NS::Graphics::HumanoidBone::Chest)], 2); // Spine2→Chest
}

TEST(RetargetTest, ComputeGlobalsChainsAndCancelsRootTransform)
{
    std::vector<NS::Graphics::Bone> bones(2);
    bones[0].parentIndex = -1;
    bones[0].bindLocal.translation = NS::Math::Vector3(1.0f, 0.0f, 0.0f);
    bones[1].parentIndex = 0;
    bones[1].bindLocal.translation = NS::Math::Vector3(0.0f, 2.0f, 0.0f);
    NS::Graphics::Skeleton skel(std::move(bones));
    skel.SetRootTransform(NS::Math::Matrix::CreateTranslation(10.0f, 0.0f, 0.0f));

    std::vector<NS::Graphics::BonePose> rest;
    for (const NS::Graphics::Bone& bone : skel.Bones())
        rest.push_back(bone.bindLocal);

    // 共通空間 (root 上位変換を打ち消し): 純粋な local チェーンだけで合成
    std::vector<NS::Math::Matrix> globals;
    skel.ComputeGlobals(rest, globals, false);
    ASSERT_EQ(globals.size(), 2u);
    EXPECT_NEAR(globals[0].Translation().x, 1.0f, 1e-5f);
    EXPECT_NEAR(globals[1].Translation().x, 1.0f, 1e-5f);
    EXPECT_NEAR(globals[1].Translation().y, 2.0f, 1e-5f);

    // root 上位変換を適用すると root 親ぶん (+10) が乗る
    skel.ComputeGlobals(rest, globals, true);
    EXPECT_NEAR(globals[0].Translation().x, 11.0f, 1e-5f);
    EXPECT_NEAR(globals[1].Translation().x, 11.0f, 1e-5f);
}

namespace
{
    NS::Math::Quaternion AxisDeg(const NS::Math::Vector3& axis, float degrees)
    {
        return NS::Math::Quaternion::CreateFromAxisAngle(axis, NS::Math::DegreesToRadians(degrees));
    }
} // namespace

TEST(RetargetTest, RetargetClipsSameRigMatchesDirect)
{
    auto makeSkel = []() {
        std::vector<NS::Graphics::Bone> bones(3);
        bones[0].name = "Hips";
        bones[0].parentIndex = -1;
        bones[0].bindLocal.translation = NS::Math::Vector3(0.0f, 1.0f, 0.0f);
        bones[1].name = "Spine";
        bones[1].parentIndex = 0;
        bones[1].bindLocal.translation = NS::Math::Vector3(0.0f, 1.0f, 0.0f);
        bones[1].bindLocal.rotation = AxisDeg(NS::Math::Vector3(0.0f, 0.0f, 1.0f), 15.0f);
        bones[2].name = "Head";
        bones[2].parentIndex = 1;
        bones[2].bindLocal.translation = NS::Math::Vector3(0.0f, 1.0f, 0.0f);
        bones[2].bindLocal.rotation = AxisDeg(NS::Math::Vector3(1.0f, 0.0f, 0.0f), 10.0f);
        return NS::Graphics::Skeleton(std::move(bones));
    };
    const NS::Graphics::Skeleton source = makeSkel();
    const NS::Graphics::Skeleton target = makeSkel();

    NS::Graphics::AnimationClip clip;
    clip.name = "walk";
    clip.duration = 1.0f;
    NS::Graphics::BoneTrack spine;
    spine.boneIndex = 1;
    spine.rotationTimes = {0.0f, 1.0f};
    spine.rotationValues = {NS::Math::Quaternion::Identity, AxisDeg(NS::Math::Vector3(0.0f, 0.0f, 1.0f), 60.0f)};
    clip.tracks.push_back(spine);
    NS::Graphics::BoneTrack head;
    head.boneIndex = 2;
    head.rotationTimes = {0.0f, 1.0f};
    head.rotationValues = {NS::Math::Quaternion::Identity, AxisDeg(NS::Math::Vector3(0.0f, 1.0f, 0.0f), 45.0f)};
    clip.tracks.push_back(head);

    // fps=4 → frames=5: t=0,.25,.5,.75,1.0 のグリッド上で直接再生と厳密一致するはず
    const std::vector<NS::Graphics::AnimationClip> rt = NS::Graphics::RetargetClips(source, {clip}, target, 4.0f);
    ASSERT_EQ(rt.size(), 1u);

    std::vector<NS::Graphics::BonePose> poseDirect;
    std::vector<NS::Graphics::BonePose> poseRt;
    float maxErr = 0.0f;
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        NS::Graphics::SampleClipPose(clip, source, t, poseDirect);
        NS::Graphics::SampleClipPose(rt[0], target, t, poseRt);
        ASSERT_EQ(poseDirect.size(), poseRt.size());
        for (std::size_t i = 0; i < poseDirect.size(); ++i)
        {
            const float dot = std::fabs(poseDirect[i].rotation.Dot(poseRt[i].rotation));
            maxErr = std::max(maxErr, 1.0f - dot);
        }
    }
    EXPECT_LT(maxErr, 1e-3f) << "同一リグの retarget が直接再生と一致しない (Concatenate 合成順を確認)";
}

TEST(RetargetTest, RetargetClipsKeepsTargetRestForUnmovedSource)
{
    // source の Spine rest = 恒等、 target の Spine rest = RotZ(30)。 anim が動かなければ target は自分の rest を保つ
    std::vector<NS::Graphics::Bone> sb(2);
    sb[0].name = "Hips";
    sb[0].parentIndex = -1;
    sb[1].name = "Spine";
    sb[1].parentIndex = 0;
    const NS::Graphics::Skeleton source(std::move(sb));

    std::vector<NS::Graphics::Bone> tb(2);
    tb[0].name = "Hips";
    tb[0].parentIndex = -1;
    tb[1].name = "Spine";
    tb[1].parentIndex = 0;
    tb[1].bindLocal.rotation = AxisDeg(NS::Math::Vector3(0.0f, 0.0f, 1.0f), 30.0f);
    const NS::Graphics::Skeleton target(std::move(tb));

    NS::Graphics::AnimationClip clip;
    clip.name = "idle";
    clip.duration = 1.0f;
    NS::Graphics::BoneTrack spine;
    spine.boneIndex = 1;
    spine.rotationTimes = {0.0f, 1.0f};
    spine.rotationValues = {NS::Math::Quaternion::Identity, NS::Math::Quaternion::Identity};
    clip.tracks.push_back(spine);

    const std::vector<NS::Graphics::AnimationClip> rt = NS::Graphics::RetargetClips(source, {clip}, target, 2.0f);
    ASSERT_EQ(rt.size(), 1u);

    std::vector<NS::Graphics::BonePose> pose;
    NS::Graphics::SampleClipPose(rt[0], target, 0.5f, pose);
    const NS::Math::Quaternion expected = AxisDeg(NS::Math::Vector3(0.0f, 0.0f, 1.0f), 30.0f);
    const float dot = std::fabs(pose[1].rotation.Dot(expected));
    EXPECT_GT(dot, 0.9999f) << "rest 差が無視され target がねじれている";
}

TEST(RetargetTest, RetargetClipsScalesHipTranslationByHeight)
{
    // source hip 高さ=1、 target hip 高さ=2 → hip 並進は 2 倍にスケール、 他骨は target rest を維持
    std::vector<NS::Graphics::Bone> sb(2);
    sb[0].name = "Hips";
    sb[0].parentIndex = -1;
    sb[0].bindLocal.translation = NS::Math::Vector3(0.0f, 1.0f, 0.0f);
    sb[1].name = "Spine";
    sb[1].parentIndex = 0;
    sb[1].bindLocal.translation = NS::Math::Vector3(0.0f, 0.5f, 0.0f);
    const NS::Graphics::Skeleton source(std::move(sb));

    std::vector<NS::Graphics::Bone> tb(2);
    tb[0].name = "Hips";
    tb[0].parentIndex = -1;
    tb[0].bindLocal.translation = NS::Math::Vector3(0.0f, 2.0f, 0.0f);
    tb[1].name = "Spine";
    tb[1].parentIndex = 0;
    tb[1].bindLocal.translation = NS::Math::Vector3(0.0f, 0.7f, 0.0f);
    const NS::Graphics::Skeleton target(std::move(tb));

    NS::Graphics::AnimationClip clip;
    clip.name = "jump";
    clip.duration = 1.0f;
    NS::Graphics::BoneTrack hips;
    hips.boneIndex = 0;
    hips.positionTimes = {0.0f, 1.0f};
    hips.positionValues = {NS::Math::Vector3(0.0f, 1.0f, 0.0f), NS::Math::Vector3(0.0f, 3.0f, 0.0f)};
    clip.tracks.push_back(hips);

    const std::vector<NS::Graphics::AnimationClip> rt = NS::Graphics::RetargetClips(source, {clip}, target, 2.0f);
    ASSERT_EQ(rt.size(), 1u);

    std::vector<NS::Graphics::BonePose> pose;
    NS::Graphics::SampleClipPose(rt[0], target, 1.0f, pose);
    // source hip 移動 = 3-1 = 2、 ×2 = 4。 target rest hip(2) + 4 = 6
    EXPECT_NEAR(pose[0].translation.y, 6.0f, 1e-3f);
    // Spine は target rest 並進を維持
    EXPECT_NEAR(pose[1].translation.y, 0.7f, 1e-3f);
}

TEST(RetargetTest, RetargetClipsTransfersWorldDeltaAcrossDifferentRests)
{
    using NS::Math::Matrix;
    using NS::Math::Quaternion;

    // rest 姿勢も骨長も違う 2 リグ。 source の動きの world 回転デルタ (rest→anim) が target に転送されるか
    auto makeChain = [](float spineDeg, float armDeg, float spineLen) {
        std::vector<NS::Graphics::Bone> b(4);
        b[0].name = "Hips";
        b[0].parentIndex = -1;
        b[0].bindLocal.translation = NS::Math::Vector3(0.0f, 1.0f, 0.0f);
        b[1].name = "Spine";
        b[1].parentIndex = 0;
        b[1].bindLocal.translation = NS::Math::Vector3(0.0f, spineLen, 0.0f);
        b[1].bindLocal.rotation = AxisDeg(NS::Math::Vector3(0.0f, 0.0f, 1.0f), spineDeg);
        b[2].name = "LeftArm";
        b[2].parentIndex = 1;
        b[2].bindLocal.translation = NS::Math::Vector3(0.3f, 0.0f, 0.0f);
        b[2].bindLocal.rotation = AxisDeg(NS::Math::Vector3(0.0f, 0.0f, 1.0f), armDeg);
        b[3].name = "LeftForeArm";
        b[3].parentIndex = 2;
        b[3].bindLocal.translation = NS::Math::Vector3(0.3f, 0.0f, 0.0f);
        return NS::Graphics::Skeleton(std::move(b));
    };
    const NS::Graphics::Skeleton source = makeChain(0.0f, 0.0f, 0.5f);    // まっすぐ rest
    const NS::Graphics::Skeleton target = makeChain(10.0f, -25.0f, 0.9f); // 違う rest / 骨長

    NS::Graphics::AnimationClip clip;
    clip.name = "wave";
    clip.duration = 1.0f;
    NS::Graphics::BoneTrack arm;
    arm.boneIndex = 2;
    arm.rotationTimes = {0.0f, 1.0f};
    arm.rotationValues = {NS::Math::Quaternion::Identity, AxisDeg(NS::Math::Vector3(0.0f, 0.0f, 1.0f), 70.0f)};
    clip.tracks.push_back(arm);
    NS::Graphics::BoneTrack fore;
    fore.boneIndex = 3;
    fore.rotationTimes = {0.0f, 1.0f};
    fore.rotationValues = {NS::Math::Quaternion::Identity, AxisDeg(NS::Math::Vector3(0.0f, 1.0f, 0.0f), 40.0f)};
    clip.tracks.push_back(fore);

    const std::vector<NS::Graphics::AnimationClip> rt = NS::Graphics::RetargetClips(source, {clip}, target, 4.0f);
    ASSERT_EQ(rt.size(), 1u);

    std::vector<NS::Graphics::BonePose> srcRest(4);
    std::vector<NS::Graphics::BonePose> dstRest(4);
    for (std::size_t i = 0; i < 4; ++i)
    {
        srcRest[i] = source.Bones()[i].bindLocal;
        dstRest[i] = target.Bones()[i].bindLocal;
    }
    std::vector<Matrix> srcRestW;
    std::vector<Matrix> dstRestW;
    source.ComputeGlobals(srcRest, srcRestW, false);
    target.ComputeGlobals(dstRest, dstRestW, false);

    auto rotOnly = [](const Matrix& m) {
        return Matrix::CreateFromQuaternion(Quaternion::CreateFromRotationMatrix(m));
    };
    auto worldDelta = [&](const Matrix& rest, const Matrix& anim) {
        return Quaternion::CreateFromRotationMatrix(rotOnly(rest).Transpose() * rotOnly(anim));
    };

    std::vector<NS::Graphics::BonePose> srcPose;
    std::vector<NS::Graphics::BonePose> dstPose;
    std::vector<Matrix> srcAnimW;
    std::vector<Matrix> dstAnimW;
    float maxErr = 0.0f;
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        NS::Graphics::SampleClipPose(clip, source, t, srcPose);
        NS::Graphics::SampleClipPose(rt[0], target, t, dstPose);
        source.ComputeGlobals(srcPose, srcAnimW, false);
        target.ComputeGlobals(dstPose, dstAnimW, false);
        for (std::size_t i = 1; i < 4; ++i) // Spine / LeftArm / LeftForeArm
        {
            const Quaternion ds = worldDelta(srcRestW[i], srcAnimW[i]);
            const Quaternion dd = worldDelta(dstRestW[i], dstAnimW[i]);
            maxErr = std::max(maxErr, 1.0f - std::fabs(ds.Dot(dd)));
        }
    }
    std::cout << "[retarget] cross-rig world-delta maxErr=" << maxErr << "\n";
    EXPECT_LT(maxErr, 1e-3f) << "異リグで world 回転デルタが転送されていない";
}
