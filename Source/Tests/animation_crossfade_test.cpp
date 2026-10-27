#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/Animation/Retarget.h"
#include "NSlib/Core/Animation/Skeleton.h"
#include "NSlib/Core/Geometry/GltfParser.h"
#include "NSlib/Core/Windows/FileSystem.h"
#include "NSlib/CoreObject/Asset/AnimClip.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace
{
    constexpr float k_Dt = 1.0f / 60.0f;
    constexpr float k_BlendSeconds = 0.12f;

    // Xbot.glb の骨格と、run・sad_pose のクリップ。メッシュは作らない
    class AnimationCrossfadeTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_source = NS::LoadGltfAnimationSource(NS::OS::FileSystem::Combine(
                NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets/Models"), "Xbot.glb"));
            ASSERT_TRUE(m_source.IsValid());
            m_run = m_assets.LoadAnimClip("Assets/Models/Xbot.glb#run")->Clip();
            m_sad = m_assets.LoadAnimClip("Assets/Models/Xbot.glb#sad_pose")->Clip();
            ASSERT_NE(m_run, nullptr);
            ASSERT_NE(m_sad, nullptr);
            m_animation.SetSkeleton(&m_source.skeleton);
        }

        // クリップだけの姿勢。補間を通さない
        std::vector<NS::Anim::BonePose> ClipPose(const NS::Anim::AnimClip* clip, float time) const
        {
            std::vector<NS::Anim::BonePose> pose;
            NS::Anim::SampleClipPose(
                *clip, NS::Anim::FindTrackBones(*clip, m_source.skeleton), m_source.skeleton, time, pose);
            return pose;
        }

        std::vector<NS::Anim::BonePose> CurrentPose() const
        {
            const std::span<const NS::Anim::BonePose> pose = m_animation.LocalPose();
            return {pose.begin(), pose.end()};
        }

        // run を frames フレーム走らせる
        void RunFrames(int frames)
        {
            m_animation.Play(m_run);
            for (int i = 0; i < frames; ++i)
            {
                m_animation.Update(&m_model, k_Dt);
            }
        }

        NS::AssetManager m_assets{NS::OS::FileSystem::ContentRoot()};
        NS::AnimSource m_source;
        const NS::Anim::AnimClip* m_run = nullptr;
        const NS::Anim::AnimClip* m_sad = nullptr;
        NS::Model m_model;
        NS::Animation m_animation;
    };

    void ExpectPoseNear(std::span<const NS::Anim::BonePose> actual, std::span<const NS::Anim::BonePose> expected)
    {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            EXPECT_NEAR(NS::Vector3::Distance(actual[i].translation, expected[i].translation), 0.0f, 1e-4f) << i;
            EXPECT_NEAR(std::fabs(actual[i].rotation.Dot(expected[i].rotation)), 1.0f, 1e-4f) << i;
            EXPECT_NEAR(NS::Vector3::Distance(actual[i].scale, expected[i].scale), 0.0f, 1e-4f) << i;
        }
    }

    // 2 つの姿勢の間で、一番大きく回った骨の角度 (ラジアン)
    float MaxBoneTurn(std::span<const NS::Anim::BonePose> a, std::span<const NS::Anim::BonePose> b)
    {
        float maxTurn = 0.0f;
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        {
            const float dot = std::min(std::fabs(a[i].rotation.Dot(b[i].rotation)), 1.0f);
            maxTurn = std::max(maxTurn, 2.0f * std::acos(dot));
        }
        return maxTurn;
    }
} // namespace

// 前のクリップが無い時と有効でない時は混ぜず、新しいクリップの姿勢から始める
TEST_F(AnimationCrossfadeTest, NoPreviousClipStartsWithoutBlending)
{
    m_animation.Play(m_run, k_BlendSeconds);
    m_animation.Update(&m_model, 0.0f);
    ExpectPoseNear(m_animation.LocalPose(), ClipPose(m_run, 0.0f));

    const NS::Anim::AnimClip invalid;
    m_animation.Play(&invalid);
    m_animation.Update(&m_model, k_Dt);
    m_animation.Play(m_sad, k_BlendSeconds);
    m_animation.Update(&m_model, 0.0f);
    ExpectPoseNear(m_animation.LocalPose(), ClipPose(m_sad, 0.0f));
}

// 切り替えの後は、保存した姿勢から新しいクリップの姿勢へ、経過の秒の割合で移り、秒を過ぎたら新しいクリップだけになる
TEST_F(AnimationCrossfadeTest, BlendsFromTheSavedPoseOverTheSeconds)
{
    RunFrames(20);
    const std::vector<NS::Anim::BonePose> saved = CurrentPose();

    m_animation.Play(m_sad, k_BlendSeconds);
    m_animation.Update(&m_model, k_BlendSeconds * 0.5f);
    std::vector<NS::Anim::BonePose> expected;
    NS::Anim::BlendPose(saved, ClipPose(m_sad, m_animation.Time()), 0.5f, expected);
    ExpectPoseNear(m_animation.LocalPose(), expected);

    m_animation.Update(&m_model, k_BlendSeconds);
    ExpectPoseNear(m_animation.LocalPose(), ClipPose(m_sad, m_animation.Time()));
}

// 補間の途中でまた切り替えると、その時の混ぜた姿勢から混ぜ直すので、姿勢が飛ばない
TEST_F(AnimationCrossfadeTest, SwitchingAgainMidBlendStartsFromTheBlendedPose)
{
    RunFrames(20);
    m_animation.Play(m_sad, k_BlendSeconds);
    m_animation.Update(&m_model, k_BlendSeconds * 0.5f);
    const std::vector<NS::Anim::BonePose> blended = CurrentPose();

    m_animation.Play(m_run, k_BlendSeconds);
    m_animation.Update(&m_model, 0.0f);
    ExpectPoseNear(m_animation.LocalPose(), blended);
}

// 骨格を替えた直後の切り替えは、前の骨格で作った姿勢と混ぜず、新しいクリップの姿勢から始める
TEST_F(AnimationCrossfadeTest, SwitchingRightAfterASkeletonChangeDoesNotBlend)
{
    RunFrames(20);
    m_animation.SetSkeleton(&m_source.skeleton);
    m_animation.Play(m_sad, k_BlendSeconds);
    m_animation.Update(&m_model, 0.0f);
    ExpectPoseNear(m_animation.LocalPose(), ClipPose(m_sad, 0.0f));
}

// 補間の途中で一時停止すると、クリップの時刻と同じく補間も止まり、姿勢が動かない
TEST_F(AnimationCrossfadeTest, PausingMidBlendHoldsThePose)
{
    RunFrames(20);
    m_animation.Play(m_sad, k_BlendSeconds);
    m_animation.Update(&m_model, k_Dt);
    m_animation.Pause();
    m_animation.Update(&m_model, k_Dt);
    const std::vector<NS::Anim::BonePose> paused = CurrentPose();

    m_animation.Update(&m_model, k_BlendSeconds);
    ExpectPoseNear(m_animation.LocalPose(), paused);
}

// 一時停止の間の切り替えも、止めた姿勢から混ぜ、新しいクリップを再生する
TEST_F(AnimationCrossfadeTest, SwitchingWhilePausedBlendsAndPlays)
{
    RunFrames(20);
    m_animation.Pause();
    const std::vector<NS::Anim::BonePose> paused = CurrentPose();

    m_animation.Play(m_sad, k_BlendSeconds);
    m_animation.Update(&m_model, k_BlendSeconds * 0.5f);
    EXPECT_TRUE(m_animation.IsPlaying());
    std::vector<NS::Anim::BonePose> expected;
    NS::Anim::BlendPose(paused, ClipPose(m_sad, m_animation.Time()), 0.5f, expected);
    ExpectPoseNear(m_animation.LocalPose(), expected);
}

// ループしないクリップが終わって止まった後の切り替えも、最後の姿勢から混ぜ、新しいクリップを再生する
TEST_F(AnimationCrossfadeTest, SwitchingAfterANonLoopingClipEndsBlendsAndPlays)
{
    m_animation.SetLooping(false);
    m_animation.Play(m_sad);
    for (int i = 0; i < 120; ++i)
    {
        m_animation.Update(&m_model, k_Dt);
    }
    ASSERT_FALSE(m_animation.IsPlaying());
    const std::vector<NS::Anim::BonePose> ended = CurrentPose();

    m_animation.Play(m_run, k_BlendSeconds);
    m_animation.Update(&m_model, k_BlendSeconds * 0.5f);
    EXPECT_TRUE(m_animation.IsPlaying());
    EXPECT_GT(m_animation.Time(), 0.0f);
    std::vector<NS::Anim::BonePose> expected;
    NS::Anim::BlendPose(ended, ClipPose(m_run, m_animation.Time()), 0.5f, expected);
    ExpectPoseNear(m_animation.LocalPose(), expected);
}

// ループしないクリップが補間より先に終わっても、補間は最後まで進み、クリップの最後の姿勢になる
TEST_F(AnimationCrossfadeTest, BlendFinishesAfterANonLoopingClipEnds)
{
    RunFrames(20);
    m_animation.SetLooping(false);
    m_animation.Play(m_sad, 1.0f);
    const float duration = m_animation.Duration();
    ASSERT_LT(duration, 1.0f);
    for (int i = 0; i < 120; ++i)
    {
        m_animation.Update(&m_model, k_Dt);
    }
    EXPECT_FALSE(m_animation.IsPlaying());
    ExpectPoseNear(m_animation.LocalPose(), ClipPose(m_sad, duration));
}

// 補間は再生速度によらず、実際の秒で終わる。速度が 0 でも前の姿勢のまま残らない
TEST_F(AnimationCrossfadeTest, BlendLastsTheSecondsWhateverTheSpeed)
{
    RunFrames(20);
    m_animation.SetSpeed(0.0f);
    m_animation.Play(m_sad, k_BlendSeconds);
    m_animation.Update(&m_model, k_BlendSeconds);
    ExpectPoseNear(m_animation.LocalPose(), ClipPose(m_sad, 0.0f));
}

// 補間の途中で Stop すると、混ぜた姿勢で止まらず、新しいクリップの最初の姿勢になる
TEST_F(AnimationCrossfadeTest, StoppingMidBlendShowsTheFirstFrame)
{
    RunFrames(20);
    m_animation.Play(m_sad, k_BlendSeconds);
    m_animation.Update(&m_model, k_Dt);
    m_animation.Stop();
    m_animation.Update(&m_model, k_Dt);
    ExpectPoseNear(m_animation.LocalPose(), ClipPose(m_sad, 0.0f));
}

// 秒が 0 の時は混ぜず、すぐ新しいクリップの姿勢になる
TEST_F(AnimationCrossfadeTest, ZeroSecondsSwitchesAtOnce)
{
    RunFrames(20);
    m_animation.Play(m_sad, 0.0f);
    m_animation.Update(&m_model, 0.0f);
    ExpectPoseNear(m_animation.LocalPose(), ClipPose(m_sad, 0.0f));
}

// run から sad_pose へ切り替えて 1 フレームずつ作った姿勢で、どの骨も 1 フレームで回る量が上限より小さい (R-3)。
// 上限は、補間しない時に切り替えのフレームで一番大きく回った骨の角度の半分
TEST_F(AnimationCrossfadeTest, NoBoneTurnsTooFarInOneFrameAcrossTheSwitch)
{
    constexpr int k_RunFrames = 20;
    const float jump = MaxBoneTurn(ClipPose(m_run, k_Dt * k_RunFrames), ClipPose(m_sad, k_Dt));
    // 切り替えで大きく姿勢が変わる組でないと、上限が補間の有無を分けない
    ASSERT_GT(jump, 0.5f);
    const float limit = jump * 0.5f;

    RunFrames(k_RunFrames);
    std::vector<NS::Anim::BonePose> previous = CurrentPose();
    m_animation.Play(m_sad, k_BlendSeconds);
    float maxTurn = 0.0f;
    for (int i = 0; i < 30; ++i)
    {
        m_animation.Update(&m_model, k_Dt);
        const std::vector<NS::Anim::BonePose> current = CurrentPose();
        maxTurn = std::max(maxTurn, MaxBoneTurn(previous, current));
        previous = current;
    }
    EXPECT_LT(maxTurn, limit) << "補間しない時の切り替えの回る量: " << jump;
}
