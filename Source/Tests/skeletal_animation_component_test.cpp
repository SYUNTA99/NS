#include <array>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Filesystem.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/Animation.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/SkeletalMesh.h>
#include <Runtime/Graphics/Skeleton.h>
#include <Runtime/Math/Math.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/MeshRendererComponent.h>
#include <Runtime/Object/Components/SkeletalAnimationComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Scene/SceneData.h>
#include <Runtime/Platform/Window.h>
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
    using NS::Object::AssetManager;
    using NS::Object::BuildSceneObject;
    using NS::Object::GameObject;
    using NS::Object::MeshRendererComponent;
    using NS::Object::ObjectData;
    using NS::Object::SkeletalAnimationComponent;

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
    const std::vector<AnimationClip> clips = OneClip(1.0f);
    SkeletalAnimationComponent comp;
    comp.AddClips(clips);
    EXPECT_EQ(comp.ClipCount(), 1u);
    EXPECT_NEAR(comp.Duration(), 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(comp.Time(), 0.0f);
    EXPECT_TRUE(comp.IsPlaying());
}

TEST(SkeletalAnimationComponentTest, OnUpdateAdvancesTimeByFixedDelta)
{
    const std::vector<AnimationClip> clips = OneClip(1.0f);
    SkeletalAnimationComponent comp;
    comp.AddClips(clips);
    comp.OnUpdate();
    EXPECT_NEAR(comp.Time(), NS::Core::FrameTimer::FixedDelta(), 1e-5f);
}

TEST(SkeletalAnimationComponentTest, PauseHoldsTime)
{
    const std::vector<AnimationClip> clips = OneClip(1.0f);
    SkeletalAnimationComponent comp;
    comp.AddClips(clips);
    comp.OnUpdate();
    const float held = comp.Time();
    comp.Pause();
    comp.OnUpdate();
    EXPECT_FLOAT_EQ(comp.Time(), held);
    EXPECT_FALSE(comp.IsPlaying());
}

TEST(SkeletalAnimationComponentTest, SetSpeedScalesAdvance)
{
    const std::vector<AnimationClip> clips = OneClip(10.0f);
    SkeletalAnimationComponent comp;
    comp.AddClips(clips);
    comp.SetSpeed(3.0f);
    comp.OnUpdate();
    EXPECT_NEAR(comp.Time(), 3.0f * NS::Core::FrameTimer::FixedDelta(), 1e-5f);
}

TEST(SkeletalAnimationComponentTest, NegativeSpeedClampsToZero)
{
    const std::vector<AnimationClip> clips = OneClip(1.0f);
    SkeletalAnimationComponent comp;
    comp.AddClips(clips);
    comp.SetSpeed(-5.0f);
    comp.OnUpdate();
    EXPECT_FLOAT_EQ(comp.Time(), 0.0f);
}

TEST(SkeletalAnimationComponentTest, StopResetsTime)
{
    const std::vector<AnimationClip> clips = OneClip(1.0f);
    SkeletalAnimationComponent comp;
    comp.AddClips(clips);
    comp.OnUpdate();
    EXPECT_GT(comp.Time(), 0.0f);
    comp.Stop();
    EXPECT_FLOAT_EQ(comp.Time(), 0.0f);
    EXPECT_FALSE(comp.IsPlaying());
}

TEST(SkeletalAnimationComponentTest, LoopingWrapsTime)
{
    const std::vector<AnimationClip> clips = OneClip(0.02f);
    SkeletalAnimationComponent comp;
    comp.AddClips(clips);
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
    const std::vector<AnimationClip> clips = OneClip(0.02f);
    SkeletalAnimationComponent comp;
    comp.AddClips(clips);
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
    const std::vector<AnimationClip> clips{MakeClip("walk", 1.0f), MakeClip("run", 0.5f)};
    SkeletalAnimationComponent comp;
    comp.AddClips(clips);
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

TEST(SkeletalAnimationComponentTest, AddClipsAppendsAndKeepsSelection)
{
    const std::vector<AnimationClip> first = OneClip(10.0f);
    std::vector<AnimationClip> more;
    more.push_back(MakeClip("run", 0.5f));

    SkeletalAnimationComponent comp;
    comp.AddClips(first);
    comp.OnUpdate();
    const float held = comp.Time();
    EXPECT_GT(held, 0.0f);

    comp.AddClips(more);

    EXPECT_EQ(comp.ClipCount(), 2u);
    EXPECT_EQ(comp.CurrentClip(), 0u);  // 選択は維持
    EXPECT_FLOAT_EQ(comp.Time(), held); // 再生位置も維持
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
    std::unique_ptr<NS::Graphics::SkeletalMesh> meshHolder = NS::Graphics::SkeletalMesh::Create(desc);
    NS::Graphics::SkeletalMesh& mesh = *meshHolder;
    ASSERT_TRUE(mesh.IsValid());

    std::vector<NS::Graphics::Bone> bones(1);
    bones[0].parentIndex = -1;
    const Skeleton skeleton(std::move(bones));
    const std::vector<AnimationClip> clips = OneClip(1.0f);

    SkeletalAnimationComponent comp;
    comp.SetMesh(&mesh);
    comp.SetSkeleton(&skeleton);
    comp.AddClips(clips);
    comp.OnStart();
    comp.OnUpdate();
    comp.OnUpdate();
    SUCCEED();
}

TEST_F(SkeletalAnimationMeshTest, ResolveAssetsDoesNothingWhenModelRefEmpty)
{
    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    SkeletalAnimationComponent comp;
    comp.ResolveAssets(am);
    EXPECT_EQ(comp.ClipCount(), 0u);
}

TEST_F(SkeletalAnimationMeshTest, ResolveAssetsDoesNothingWhenModelRefEscapesContentRoot)
{
    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    SkeletalAnimationComponent comp;
    comp.SetModelRef("../outside_content_root.glb");
    comp.ResolveAssets(am);
    EXPECT_EQ(comp.ClipCount(), 0u);
}

TEST_F(SkeletalAnimationMeshTest, ResolveAssetsDoesNothingWhenModelFileMissing)
{
    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    GameObject obj;
    auto& renderer = *obj.AddComponent<MeshRendererComponent>();
    auto& comp = *obj.AddComponent<SkeletalAnimationComponent>();
    comp.SetModelRef("__ns_sac_missing_model__.glb");
    comp.ResolveAssets(am);
    EXPECT_EQ(comp.ClipCount(), 0u);
    // 失敗時は同じ object の mesh に触らない
    EXPECT_EQ(renderer.GetMesh(), nullptr);
}

TEST_F(SkeletalAnimationMeshTest, ResolveAssetsWiresRealSkinnedModelAndSiblingMesh)
{
    const std::filesystem::path modelPath = NS::Core::FileSystem::ContentRoot() / "Assets" / "Models" / "CesiumMan.glb";
    if (!std::filesystem::exists(modelPath))
        GTEST_SKIP() << "CesiumMan.glb が無い: " << modelPath.string();

    NS::Platform::WindowDesc wd{};
    wd.title = "ns_anim_resolve";
    wd.size = NS::Math::Size2D{320, 240};
    wd.visible = false;
    NS::Platform::Window window(wd);
    ASSERT_TRUE(window.IsValid());

    NS::Graphics::RendererDesc rd{};
    rd.vsync = false;
    rd.enableDebugLayer = false;
    NS::Graphics::Renderer renderer(rd, window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};

    GameObject obj;
    auto& meshComp = *obj.AddComponent<MeshRendererComponent>();
    auto& anim = *obj.AddComponent<SkeletalAnimationComponent>();
    anim.SetModelRef("Assets/Models/CesiumMan.glb");

    anim.ResolveAssets(am);

    // どの mesh が差さったかまで確かめる。キャッシュの重複排除で同一 path は同一ポインタになる
    const NS::Object::LoadedSkinnedModel loaded = am.GetOrLoadSkinnedModel(modelPath);
    ASSERT_TRUE(loaded.valid);
    EXPECT_GT(anim.ClipCount(), 0u);
    EXPECT_EQ(meshComp.GetMesh(), loaded.mesh);

    // skeleton もキャッシュ record への参照なので、同一 path の再取得で同一ポインタになる
    const NS::Object::LoadedSkinnedModel loadedAgain = am.GetOrLoadSkinnedModel(modelPath);
    ASSERT_TRUE(loadedAgain.valid);
    EXPECT_NE(loaded.skeleton, nullptr);
    EXPECT_EQ(loaded.skeleton, loadedAgain.skeleton);
}

// Clips 欄の自己結合で model 由来クリップが倍加し、結合後の boneIndex が骨数に収まるのを確かめる
TEST_F(SkeletalAnimationMeshTest, ResolveAssetsAddsClipsFromClipsRef)
{
    const std::filesystem::path modelPath = NS::Core::FileSystem::ContentRoot() / "Assets" / "Models" / "CesiumMan.glb";
    if (!std::filesystem::exists(modelPath))
        GTEST_SKIP() << "CesiumMan.glb が無い: " << modelPath.string();

    NS::Platform::WindowDesc wd{};
    wd.title = "ns_anim_clips_ref";
    wd.size = NS::Math::Size2D{320, 240};
    wd.visible = false;
    NS::Platform::Window window(wd);
    ASSERT_TRUE(window.IsValid());

    NS::Graphics::RendererDesc rd{};
    rd.vsync = false;
    rd.enableDebugLayer = false;
    NS::Graphics::Renderer renderer(rd, window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};

    GameObject obj;
    auto& anim = *obj.AddComponent<SkeletalAnimationComponent>();
    anim.SetModelRef("Assets/Models/CesiumMan.glb");
    anim.SetClipsRef("Assets/Models/CesiumMan.glb");
    anim.ResolveAssets(am);

    const NS::Object::LoadedSkinnedModel loaded = am.GetOrLoadSkinnedModel(modelPath);
    ASSERT_TRUE(loaded.valid);
    ASSERT_NE(loaded.clips, nullptr);
    ASSERT_NE(loaded.skeleton, nullptr);
    EXPECT_EQ(anim.ClipCount(), loaded.clips->size() * 2u);

    const std::vector<AnimationClip>* bound = am.GetOrLoadBoundClips(modelPath, modelPath);
    ASSERT_NE(bound, nullptr);
    const int boneCount = static_cast<int>(loaded.skeleton->BoneCount());
    for (const AnimationClip& clip : *bound)
    {
        for (const BoneTrack& track : clip.tracks)
        {
            EXPECT_GE(track.boneIndex, 0);
            EXPECT_LT(track.boneIndex, boneCount);
        }
    }
}

// 解決できないエントリが混ざっても他のエントリを巻き込まない
TEST_F(SkeletalAnimationMeshTest, ResolveAssetsSkipsBadClipEntriesIndependently)
{
    const std::filesystem::path modelPath = NS::Core::FileSystem::ContentRoot() / "Assets" / "Models" / "CesiumMan.glb";
    if (!std::filesystem::exists(modelPath))
        GTEST_SKIP() << "CesiumMan.glb が無い: " << modelPath.string();

    NS::Platform::WindowDesc wd{};
    wd.title = "ns_anim_clips_bad";
    wd.size = NS::Math::Size2D{320, 240};
    wd.visible = false;
    NS::Platform::Window window(wd);
    ASSERT_TRUE(window.IsValid());

    NS::Graphics::RendererDesc rd{};
    rd.vsync = false;
    rd.enableDebugLayer = false;
    NS::Graphics::Renderer renderer(rd, window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};

    GameObject obj;
    auto& anim = *obj.AddComponent<SkeletalAnimationComponent>();
    anim.SetModelRef("Assets/Models/CesiumMan.glb");
    anim.SetClipsRef(" ;__ns_missing__.glb; Assets/Models/CesiumMan.glb ;../escape.glb");
    anim.ResolveAssets(am);

    const NS::Object::LoadedSkinnedModel loaded = am.GetOrLoadSkinnedModel(modelPath);
    ASSERT_TRUE(loaded.valid);
    ASSERT_NE(loaded.clips, nullptr);
    // 空白・不在・トラバーサルの 3 エントリは skip され、有効な 1 エントリの分だけ増える
    EXPECT_EQ(anim.ClipCount(), loaded.clips->size() * 2u);
}

// 同じ clip と model の組は結合済コピーをインスタンス間で共有する
TEST_F(SkeletalAnimationMeshTest, ResolveAssetsSharesBoundClipsBetweenInstances)
{
    const std::filesystem::path modelPath = NS::Core::FileSystem::ContentRoot() / "Assets" / "Models" / "CesiumMan.glb";
    if (!std::filesystem::exists(modelPath))
        GTEST_SKIP() << "CesiumMan.glb が無い: " << modelPath.string();

    NS::Platform::WindowDesc wd{};
    wd.title = "ns_anim_clips_share";
    wd.size = NS::Math::Size2D{320, 240};
    wd.visible = false;
    NS::Platform::Window window(wd);
    ASSERT_TRUE(window.IsValid());

    NS::Graphics::RendererDesc rd{};
    rd.vsync = false;
    rd.enableDebugLayer = false;
    NS::Graphics::Renderer renderer(rd, window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};

    GameObject objA;
    auto& animA = *objA.AddComponent<SkeletalAnimationComponent>();
    animA.SetModelRef("Assets/Models/CesiumMan.glb");
    animA.SetClipsRef("Assets/Models/CesiumMan.glb");
    animA.ResolveAssets(am);
    const std::vector<AnimationClip>* boundFirst = am.GetOrLoadBoundClips(modelPath, modelPath);
    ASSERT_NE(boundFirst, nullptr);

    GameObject objB;
    auto& animB = *objB.AddComponent<SkeletalAnimationComponent>();
    animB.SetModelRef("Assets/Models/CesiumMan.glb");
    animB.SetClipsRef("Assets/Models/CesiumMan.glb");
    animB.ResolveAssets(am);
    const std::vector<AnimationClip>* boundSecond = am.GetOrLoadBoundClips(modelPath, modelPath);

    // 2 体目の解決後も同じ実体が返り、双方の component が同じ結合済コピーを参照している
    EXPECT_EQ(boundFirst, boundSecond);
    EXPECT_EQ(animA.ClipCount(), animB.ClipCount());
}

// 組立の全経路で cube 既定を skinned mesh が上書きする並び (MeshRenderer が先・こちらが後) を固定する
TEST_F(SkeletalAnimationMeshTest, BuildSceneObjectOverridesMeshRendererWithSkinnedMesh)
{
    const std::filesystem::path modelPath = NS::Core::FileSystem::ContentRoot() / "Assets" / "Models" / "CesiumMan.glb";
    if (!std::filesystem::exists(modelPath))
        GTEST_SKIP() << "CesiumMan.glb が無い: " << modelPath.string();

    NS::Platform::WindowDesc wd{};
    wd.title = "ns_anim_build_order";
    wd.size = NS::Math::Size2D{320, 240};
    wd.visible = false;
    NS::Platform::Window window(wd);
    ASSERT_TRUE(window.IsValid());

    NS::Graphics::RendererDesc rd{};
    rd.vsync = false;
    rd.enableDebugLayer = false;
    NS::Graphics::Renderer renderer(rd, window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "Device 確立不可 (headless)";

    AssetManager am{NS::Core::FileSystem::ContentRoot()};
    am.RegisterBuiltins();

    ObjectData data;
    nlohmann::json meshEntry = NS::Object::MakeComponentEntry("MeshRendererComponent");
    NS::Object::SetField(meshEntry, "メッシュ", std::string("cube"));
    data.components.push_back(meshEntry);
    nlohmann::json animEntry = NS::Object::MakeComponentEntry("SkeletalAnimationComponent");
    NS::Object::SetField(animEntry, "モデル", std::string("Assets/Models/CesiumMan.glb"));
    data.components.push_back(animEntry);

    auto built = BuildSceneObject(data, &am);
    ASSERT_NE(built, nullptr);
    auto* meshComp = built->FindComponent<MeshRendererComponent>();
    ASSERT_NE(meshComp, nullptr);

    const NS::Object::LoadedSkinnedModel loaded = am.GetOrLoadSkinnedModel(modelPath);
    ASSERT_TRUE(loaded.valid);
    EXPECT_EQ(meshComp->GetMesh(), loaded.mesh);
}
