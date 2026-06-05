#include <gtest/gtest.h>

#include <Framework/Core/Clock.h>
#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Animation.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Graphics/SkeletalMesh.h>
#include <Framework/Graphics/Skeleton.h>
#include <Framework/Math/Math.h>
#include <Framework/Platform/Window.h>
#include <Framework/Scene/SkeletalAnimationComponent.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using NS::Graphics::AnimationClip;
    using NS::Graphics::BoneTrack;
    using NS::Graphics::Interpolation;
    using NS::Graphics::Skeleton;
    using NS::Math::Quaternion;
    using NS::Math::Vector3;
    using NS::Scene::SkeletalAnimationComponent;

    Quaternion RotZ(float degrees)
    {
        return Quaternion::CreateFromAxisAngle(Vector3(0.0f, 0.0f, 1.0f), NS::Math::DegreesToRadians(degrees));
    }

    AnimationClip MakeClip(const std::string& name, float duration)
    {
        AnimationClip clip;
        clip.name = name;
        clip.duration = duration;
        BoneTrack track;
        track.boneIndex = 0;
        track.rotationTimes = {0.0f, duration};
        track.rotationValues = {Quaternion::Identity, RotZ(90.0f)};
        track.rotationInterp = Interpolation::Linear;
        clip.tracks.push_back(track);
        return clip;
    }

    std::vector<AnimationClip> OneClip(float duration)
    {
        return {MakeClip("walk", duration)};
    }
} // namespace

TEST(SkeletalAnimationComponentTest, InitialState)
{
    SkeletalAnimationComponent comp(nullptr, Skeleton{}, OneClip(1.0f));
    EXPECT_EQ(comp.ClipCount(), 1u);
    EXPECT_NEAR(comp.Duration(), 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(comp.Time(), 0.0f);
    EXPECT_TRUE(comp.IsPlaying());
}

TEST(SkeletalAnimationComponentTest, OnUpdateAdvancesTimeByFixedDelta)
{
    SkeletalAnimationComponent comp(nullptr, Skeleton{}, OneClip(1.0f));
    comp.OnUpdate();
    EXPECT_NEAR(comp.Time(), NS::Core::FrameTimer::FixedDelta(), 1e-5f);
}

TEST(SkeletalAnimationComponentTest, PauseHoldsTime)
{
    SkeletalAnimationComponent comp(nullptr, Skeleton{}, OneClip(1.0f));
    comp.OnUpdate();
    const float held = comp.Time();
    comp.Pause();
    comp.OnUpdate();
    EXPECT_FLOAT_EQ(comp.Time(), held);
    EXPECT_FALSE(comp.IsPlaying());
}

TEST(SkeletalAnimationComponentTest, SetSpeedScalesAdvance)
{
    SkeletalAnimationComponent comp(nullptr, Skeleton{}, OneClip(10.0f));
    comp.SetSpeed(3.0f);
    comp.OnUpdate();
    EXPECT_NEAR(comp.Time(), 3.0f * NS::Core::FrameTimer::FixedDelta(), 1e-5f);
}

TEST(SkeletalAnimationComponentTest, NegativeSpeedClampsToZero)
{
    SkeletalAnimationComponent comp(nullptr, Skeleton{}, OneClip(1.0f));
    comp.SetSpeed(-5.0f);
    comp.OnUpdate();
    EXPECT_FLOAT_EQ(comp.Time(), 0.0f);
}

TEST(SkeletalAnimationComponentTest, StopResetsTime)
{
    SkeletalAnimationComponent comp(nullptr, Skeleton{}, OneClip(1.0f));
    comp.OnUpdate();
    EXPECT_GT(comp.Time(), 0.0f);
    comp.Stop();
    EXPECT_FLOAT_EQ(comp.Time(), 0.0f);
    EXPECT_FALSE(comp.IsPlaying());
}

TEST(SkeletalAnimationComponentTest, LoopingWrapsTime)
{
    SkeletalAnimationComponent comp(nullptr, Skeleton{}, OneClip(0.02f));
    comp.SetLooping(true);
    for (int i = 0; i < 5; ++i)
    {
        comp.OnUpdate();
    }
    EXPECT_GE(comp.Time(), 0.0f);
    EXPECT_LT(comp.Time(), 0.02f);
    EXPECT_TRUE(comp.IsPlaying());
}

TEST(SkeletalAnimationComponentTest, NonLoopingStopsAtEnd)
{
    SkeletalAnimationComponent comp(nullptr, Skeleton{}, OneClip(0.02f));
    comp.SetLooping(false);
    for (int i = 0; i < 5; ++i)
    {
        comp.OnUpdate();
    }
    EXPECT_NEAR(comp.Time(), 0.02f, 1e-5f);
    EXPECT_FALSE(comp.IsPlaying());
}

TEST(SkeletalAnimationComponentTest, SelectClip)
{
    std::vector<AnimationClip> clips{MakeClip("walk", 1.0f), MakeClip("run", 0.5f)};
    SkeletalAnimationComponent comp(nullptr, Skeleton{}, std::move(clips));
    EXPECT_EQ(comp.ClipCount(), 2u);

    comp.OnUpdate();
    EXPECT_GT(comp.Time(), 0.0f);

    ASSERT_TRUE(comp.SelectClip(1));
    EXPECT_EQ(comp.CurrentClip(), 1u);
    EXPECT_NEAR(comp.Duration(), 0.5f, 1e-5f);
    EXPECT_FLOAT_EQ(comp.Time(), 0.0f);

    EXPECT_FALSE(comp.SelectClip(99));
    EXPECT_TRUE(comp.SelectClip("walk"));
    EXPECT_EQ(comp.CurrentClip(), 0u);
    EXPECT_FALSE(comp.SelectClip("nope"));
}

class SkeletalAnimationMeshTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(SkeletalAnimationMeshTest, DrivesMeshPaletteWithoutCrash)
{
    NS::Platform::WindowDesc wd{};
    wd.title = "ns_anim_comp";
    wd.size = NS::Math::Size2D{320, 240};
    wd.visible = false;
    NS::Platform::Window window(wd);
    ASSERT_TRUE(window.IsValid());

    NS::Graphics::RendererDesc rd{};
    rd.vsync = false;
    rd.enableDebugLayer = false;
    NS::Graphics::Renderer renderer(rd, window);
    ASSERT_TRUE(renderer.IsValid());

    std::array<NS::Graphics::SkinnedVertex, 3> verts{};
    for (NS::Graphics::SkinnedVertex& v : verts)
    {
        v.normal = Vector3(0.0f, 0.0f, -1.0f);
        v.joints[0] = 0;
        v.weights[0] = 1.0f;
    }
    verts[0].position = Vector3(-1.0f, -1.0f, 0.0f);
    verts[1].position = Vector3(0.0f, 1.0f, 0.0f);
    verts[2].position = Vector3(1.0f, -1.0f, 0.0f);
    const std::array<std::uint32_t, 3> idx{0, 1, 2};

    NS::Graphics::SkinnedMeshDesc desc{};
    desc.vertices = verts.data();
    desc.vertexCount = verts.size();
    desc.indices = idx.data();
    desc.indexCount = idx.size();
    desc.boneCount = 1;
    NS::Graphics::SkeletalMesh mesh(renderer, desc);
    ASSERT_TRUE(mesh.IsValid());

    std::vector<NS::Graphics::Bone> bones(1);
    bones[0].parentIndex = -1;
    Skeleton skeleton(std::move(bones));

    SkeletalAnimationComponent comp(&mesh, std::move(skeleton), OneClip(1.0f));
    comp.OnStart();
    comp.OnUpdate();
    comp.OnUpdate();
    SUCCEED();
}
