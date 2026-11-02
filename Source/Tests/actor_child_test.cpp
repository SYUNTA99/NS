#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/ChildActorObj.h"
#include "NSlib/CoreObject/System/SceneRenderer.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "Tests/TestViewCamera.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Window.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/Render/RenderContext.h"

#include <gtest/gtest.h>
#include <array>
#include <type_traits>
#include <utility>

static_assert(std::is_same_v<decltype(std::declval<NS::SceneActor&>().ChildActor()), NS::ChildActorObj*>);
static_assert(std::is_same_v<decltype(std::declval<const NS::SceneActor&>().ChildActor()), const NS::ChildActorObj*>);
static_assert(!std::derived_from<NS::SceneActor, NS::IRenderable>);

namespace
{
    class SharedModelParent final : public NS::SceneActor
    {
    protected:
        void OnInit() override
        {
            CreateClassObj(m_model);
            NS::InitDraw(this, m_model.get(), &Root());
            m_model->SetMeshRef("cube");
            m_model->SetMaterialRef("Assets/Materials/player.mat");
            CreateChildActor<NS::ChildActorObj>()->UseParentModel();
        }
    };

    struct ChildCounts
    {
        int made = 0;
        int started = 0;
        int steps = 0;
        int vanished = 0;
        int ended = 0;
        int destroyed = 0;
        int parentSteps = 0;
        bool parentStarted = false;
        bool startedAfterParent = false;
        bool endedWithParentModel = false;
        bool destroyDuringStep = false;
        bool aliveUntilStepEnd = false;
        float parentX = 0.0f;
        int assetLoads = 0;
        bool parentAssetsLoaded = false;
        bool loadedAfterParent = false;
        int placed = 0;
        bool parentPlaced = false;
        bool placedAfterParent = false;
        bool hiddenBeforeChild = false;
    };

    class ChildProbe final : public NS::ChildActorObj
    {
    public:
        explicit ChildProbe(ChildCounts& counts) : m_counts(counts) { ++m_counts.made; }
        ~ChildProbe() override { ++m_counts.destroyed; }
        NS_REFLECT_NONE(ChildProbe, NS::ChildActorObj)

        void OnStart() override
        {
            NS::SceneActor::OnStart();
            ++m_counts.started;
            m_counts.startedAfterParent = m_counts.parentStarted;
        }

        void OnEndPlay() override
        {
            m_counts.endedWithParentModel = ParentActor()->ModelClassObj() != nullptr;
            ++m_counts.ended;
            NS::ChildActorObj::OnEndPlay();
        }

        void InitAfterPlacement() override
        {
            ++m_counts.placed;
            m_counts.placedAfterParent = m_counts.parentPlaced;
        }

    protected:
        void OnAssetsLoaded() override
        {
            NS::SceneActor::OnAssetsLoaded();
            ++m_counts.assetLoads;
            m_counts.loadedAfterParent = m_counts.parentAssetsLoaded;
        }

        void VisualStep() override
        {
            ++m_counts.steps;
            m_counts.parentX = ParentActor()->Root().Position().x;
            EXPECT_EQ(m_counts.steps, m_counts.parentSteps);
            if (m_counts.destroyDuringStep)
            {
                Destroy();
                m_counts.aliveUntilStepEnd = m_counts.destroyed == 0;
            }
        }

        void OnDisappear() noexcept override
        {
            ++m_counts.vanished;
            NS::ChildActorObj::OnDisappear();
        }

    private:
        ChildCounts& m_counts;
    };

    class ParentProbe final : public NS::SceneActor
    {
    public:
        explicit ParentProbe(ChildCounts& counts) : m_counts(counts) {}
        NS_REFLECT_NONE(ParentProbe, NS::SceneActor)

        bool TryAnotherChild() { return CreateChildActor<ChildProbe>(m_counts) != nullptr; }
        void InitAfterPlacement() override { m_counts.parentPlaced = true; }

        void OnStart() override
        {
            m_counts.parentStarted = true;
            NS::SceneActor::OnStart();
        }

    protected:
        void OnInit() override
        {
            CreateClassObj(m_model);
            if (m_counts.hiddenBeforeChild)
            {
                Disappear();
            }
            (void)CreateChildActor<ChildProbe>(m_counts);
        }

        void BodyStep() override
        {
            Root().SetPosition(Root().Position() + NS::Vector3{1.0f, 0.0f, 0.0f});
        }

        void DecideStep() override { ++m_counts.parentSteps; }

        void OnAssetsLoaded() override
        {
            NS::SceneActor::OnAssetsLoaded();
            m_counts.parentAssetsLoaded = true;
        }

    private:
        ChildCounts& m_counts;
    };
}

TEST(ActorChild, BelongsToItsParentAndStartsAfterItWithoutAnotherScenePlacement)
{
    ChildCounts counts;
    NS::Scene scene;
    ParentProbe* parent = scene.SpawnActor<ParentProbe>("parent", counts);
    ASSERT_NE(parent, nullptr);
    NS::ChildActorObj* child = parent->ChildActor();
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->ParentActor(), parent);
    EXPECT_EQ(child->OwningScene(), &scene);
    EXPECT_EQ(scene.Actors().ActorCount(), 1u);
    EXPECT_EQ(counts.started, 1);
    EXPECT_TRUE(counts.startedAfterParent);
    EXPECT_FALSE(parent->TryAnotherChild());
    EXPECT_EQ(counts.made, 1);
}

TEST(ActorChild, AdvancesAfterTheParentDuringHitStopAndSharesWorldSpeed)
{
    ChildCounts counts;
    NS::Scene scene;
    ParentProbe* parent = scene.SpawnActor<ParentProbe>("parent", counts);
    ASSERT_NE(parent, nullptr);
    scene.OnUpdate();
    EXPECT_EQ(counts.steps, 1);
    EXPECT_FLOAT_EQ(counts.parentX, 1.0f);
    parent->RequestHitStop(2);
    scene.OnUpdate();
    scene.OnUpdate();
    EXPECT_EQ(counts.steps, 3);
    EXPECT_FLOAT_EQ(counts.parentX, 1.0f);
    scene.SetWorldSpeed(0.5f);
    for (int frame = 0; frame < 4; ++frame)
    {
        scene.OnUpdate();
    }
    EXPECT_EQ(counts.steps, 5);
    EXPECT_FLOAT_EQ(counts.parentX, 3.0f);
}

TEST(ActorChild, FollowsPresenceAndEndsBeforeTheParentsModelGoesAway)
{
    ChildCounts counts;
    {
        NS::Scene scene;
        ParentProbe* parent = scene.SpawnActor<ParentProbe>("parent", counts);
        ASSERT_NE(parent, nullptr);
        parent->Disappear();
        EXPECT_FALSE(parent->ChildActor()->IsAppeared());
        scene.OnUpdate();
        EXPECT_EQ(counts.steps, 0);
        parent->Appear();
        EXPECT_TRUE(parent->ChildActor()->IsAppeared());
        scene.OnUpdate();
        EXPECT_EQ(counts.steps, 1);
        parent->Destroy();
        EXPECT_EQ(counts.destroyed, 0);
        scene.OnUpdate();
        EXPECT_EQ(scene.Actors().ActorCount(), 0u);
    }
    EXPECT_EQ(counts.ended, 1);
    EXPECT_EQ(counts.destroyed, 1);
    EXPECT_TRUE(counts.endedWithParentModel);
}

TEST(ActorChild, ChildStartsHiddenWhenCreatedByAHiddenParent)
{
    ChildCounts counts;
    counts.hiddenBeforeChild = true;
    NS::Scene scene;
    ParentProbe* parent = scene.SpawnActor<ParentProbe>("parent", counts);
    ASSERT_NE(parent, nullptr);
    EXPECT_FALSE(parent->IsAppeared());
    ASSERT_NE(parent->ChildActor(), nullptr);
    EXPECT_FALSE(parent->ChildActor()->IsAppeared());
    scene.OnUpdate();
    EXPECT_EQ(counts.steps, 0);
}

TEST(ActorChild, DestroyedChildIsReleasedAtTheEndOfTheStep)
{
    ChildCounts counts;
    NS::Scene scene;
    ParentProbe* parent = scene.SpawnActor<ParentProbe>("parent", counts);
    ASSERT_NE(parent, nullptr);
    counts.destroyDuringStep = true;
    scene.OnUpdate();
    EXPECT_TRUE(counts.aliveUntilStepEnd);
    EXPECT_EQ(counts.destroyed, 1);
    EXPECT_EQ(counts.ended, 1);
    EXPECT_EQ(parent->ChildActor(), nullptr);
    EXPECT_EQ(scene.Actors().ActorCount(), 1u);
}

TEST(ActorChild, PlayerAndPropUseTheirMainModelWithoutGivingTheChildAnotherModel)
{
    NS::Scene scene;
    PlayerActor* player = scene.SpawnActor<PlayerActor>("player");
    GL::Level::PropActor* prop = scene.SpawnActor<GL::Level::PropActor>("prop");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(prop, nullptr);
    ASSERT_NE(player->ChildActor(), nullptr);
    ASSERT_NE(prop->ChildActor(), nullptr);
    EXPECT_STREQ(player->ChildActor()->ClassName(), "ChildActorObj");
    EXPECT_STREQ(prop->ChildActor()->ClassName(), "ChildActorObj");
    EXPECT_EQ(player->ChildActor()->ParentActor(), player);
    EXPECT_EQ(prop->ChildActor()->ParentActor(), prop);
    EXPECT_EQ(player->ChildActor()->ModelClassObj(), nullptr);
    EXPECT_EQ(prop->ChildActor()->ModelClassObj(), nullptr);
    EXPECT_EQ(player->ChildActor()->ModelSource(), player->ModelClassObj());
    EXPECT_EQ(prop->ChildActor()->ModelSource(), prop->ModelClassObj());
    EXPECT_EQ(scene.Actors().ActorCount(), 2u);
    NS::SceneRenderer* renderer = player->GetSceneRenderer();
    ASSERT_NE(renderer, nullptr);
    const std::size_t registered = renderer->RenderableCount();
    ASSERT_TRUE(player->Ghost()->SetSpread(NS::Vector3{0.3f, 0.0f, 0.0f}));
    ASSERT_TRUE(player->Ghost()->SetAfterimageOpacity(0.4f));
    player->Disappear();
    EXPECT_FALSE(player->ChildActor()->IsAppeared());
    EXPECT_EQ(renderer->RenderableCount(), registered);
    player->Appear();
    EXPECT_TRUE(player->ChildActor()->IsAppeared());
    EXPECT_FLOAT_EQ(player->Ghost()->Spread().x, 0.3f);
    EXPECT_FLOAT_EQ(player->Ghost()->AfterimageOpacity(), 0.4f);
    player->ChildActor()->Destroy();
    scene.OnUpdate();
    EXPECT_EQ(player->ChildActor(), nullptr);
    EXPECT_EQ(player->Ghost(), nullptr);
    EXPECT_EQ(renderer->RenderableCount(), registered - 1u);
    prop->ChildActor()->Destroy();
    scene.OnUpdate();
    EXPECT_EQ(prop->Ghost(), nullptr);
    player->Destroy();
    prop->Destroy();
    scene.OnUpdate();
    EXPECT_EQ(renderer->RenderableCount(), 0u);
}

TEST(ActorChild, OrdinaryChildDrawsTheSharedModelWithItsOwnPoseAndOpacity)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "子のモデル参照描画の試し", .size = {64, 36}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AssetManagerScope useAssets{assets};
    NS::Scene scene;
    scene.GetSceneRenderer()->SetDevice(&device);
    SharedModelParent* parent = scene.SpawnActor<SharedModelParent>("parent");
    ASSERT_NE(parent, nullptr);
    NS::ChildActorObj* child = parent->ChildActor();
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(child->ClassName(), "ChildActorObj");
    EXPECT_EQ(child->ModelClassObj(), nullptr);
    EXPECT_EQ(child->ModelSource(), parent->ModelClassObj());
    parent->Root().SetPosition(NS::Vector3{4.0f, 0.0f, 0.0f});
    child->Root().SetPosition(NS::Vector3{10.0f, 0.0f, 0.0f});
    child->Root().Snapshot();
    child->Root().SetPosition(NS::Vector3{14.0f, 0.0f, 0.0f});
    std::array<NS::ModelDraw, 2> draws{
        NS::ModelDraw{.offset = NS::Vector3{1.0f, 0.0f, 0.0f}, .opacity = 0.5f, .groundWave = false},
        NS::ModelDraw{.world = NS::Matrix::CreateTranslation(40.0f, 0.0f, 0.0f),
                       .opacity = 0.25f, .groundWave = false, .tremor = false}};
    child->SetModelDraws(draws);
    draws[0].opacity = 0.0f;
    NS::Gfx::RenderContext context{};
    context.alpha = 0.5f;
    context.groundWave.strength = 2.0f;
    std::vector<NS::Gfx::DrawItem> copies;
    child->Collect(context, NS::RenderSource{child->Root().InterpolatedWorldMatrix(context.alpha)}, copies);
    ASSERT_EQ(copies.size(), 2u);
    EXPECT_FLOAT_EQ(copies[0].constants.world._41, 13.0f);
    EXPECT_FLOAT_EQ(copies[1].constants.world._41, 40.0f);
    EXPECT_FLOAT_EQ(copies[0].constants.opacity, 0.5f);
    EXPECT_FLOAT_EQ(copies[1].constants.opacity, 0.25f);
    EXPECT_EQ(copies[0].blend, NS::Gfx::BlendMode::Alpha);
    EXPECT_FLOAT_EQ(copies[0].constants.groundWaveStrength, 0.0f);
    std::vector<NS::Gfx::DrawItem> body;
    parent->ModelClassObj()->Collect(context, NS::RenderSource{parent->Root().WorldMatrix()}, body);
    ASSERT_EQ(body.size(), 1u);
    EXPECT_EQ(copies[0].mesh, body[0].mesh);
    EXPECT_EQ(copies[1].material, body[0].material);
    EXPECT_FLOAT_EQ(body[0].constants.world._41, 4.0f);
    EXPECT_FLOAT_EQ(body[0].constants.opacity, 1.0f);
    EXPECT_FLOAT_EQ(body[0].constants.groundWaveStrength, 2.0f);
    const NS::AABB bounds = child->WorldBounds(NS::RenderSource{child->Root().WorldMatrix()});
    EXPECT_GE(bounds.Center.x + bounds.Extents.x, 40.0f);
    parent->Root().SetPosition(NS::Vector3{100.0f, 0.0f, 0.0f});
    child->Root().SetPosition(NS::Vector3{100.0f, 0.0f, 0.0f});
    copies.clear();
    child->Collect(context, NS::RenderSource{child->Root().WorldMatrix()}, copies);
    ASSERT_EQ(copies.size(), 2u);
    EXPECT_FLOAT_EQ(copies[1].constants.world._41, 40.0f);
    const std::size_t registered = parent->GetSceneRenderer()->RenderableCount();
    child->UseParentModel();
    EXPECT_EQ(parent->GetSceneRenderer()->RenderableCount(), registered);
    const std::size_t enabled = parent->GetSceneRenderer()->EnabledRenderableCount();
    child->Disappear();
    EXPECT_EQ(parent->GetSceneRenderer()->EnabledRenderableCount(), enabled - 1u);
    EXPECT_TRUE(parent->IsAppeared());
    NS::ShowModel(parent, parent->ModelClassObj());
    EXPECT_EQ(parent->GetSceneRenderer()->EnabledRenderableCount(), enabled - 1u);
    child->Appear();
    EXPECT_EQ(parent->GetSceneRenderer()->EnabledRenderableCount(), enabled);
    copies.clear();
    child->Collect(context, NS::RenderSource{child->Root().WorldMatrix()}, copies);
    EXPECT_EQ(copies.size(), 2u);
    child->OnEndPlay();
    copies.clear();
    child->Collect(context, NS::RenderSource{child->Root().WorldMatrix()}, copies);
    EXPECT_TRUE(copies.empty());
    EXPECT_EQ(parent->GetSceneRenderer()->RenderableCount(), registered - 1u);
}

TEST(ActorChild, ReloadsTheChildAssetsAfterTheParent)
{
    ChildCounts counts;
    NS::Scene scene;
    ParentProbe* parent = scene.SpawnActor<ParentProbe>("parent", counts);
    ASSERT_NE(parent, nullptr);
    EXPECT_TRUE(counts.loadedAfterParent);
    EXPECT_EQ(counts.assetLoads, 1);
    counts.parentAssetsLoaded = false;
    parent->ReloadAssets();
    EXPECT_TRUE(counts.loadedAfterParent);
    EXPECT_EQ(counts.assetLoads, 2);
}

TEST(ActorChild, NotifiesTheChildAfterItsParentIsPlaced)
{
    ChildCounts counts;
    NS::Scene scene;
    ASSERT_NE(scene.SpawnActor<ParentProbe>("parent", counts), nullptr);
    EXPECT_EQ(counts.placed, 1);
    EXPECT_TRUE(counts.placedAfterParent);
}

TEST(ActorChild, PausesTheAfterimageWhileHiddenAndContinuesAfterReappearing)
{
    NS::Scene scene;
    PlayerActor* player = scene.SpawnActor<PlayerActor>("player");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{0.0f, 1.0f, -5.0f}, NS::Vector3{}), nullptr);
    scene.OnUpdate();
    NS::HitOwnerRun run{};
    run.length = 4;
    run.item.name = "TackleGhost";
    run.item.values = {{"離れのピクセル", 30.0f},
                       {"左右の残像の不透明度", 0.4f},
                       {"明けの残像のフレーム数", 4.0f},
                       {"明けの残像の不透明度", 0.6f}};
    player->Ghost()->Start(run, player);
    scene.OnUpdate();
    const float firstSpread = player->Ghost()->Spread().Length();
    ASSERT_GT(firstSpread, 0.0f);
    EXPECT_EQ(player->ChildActor()->Root().Position(), player->Root().Position());
    EXPECT_EQ(player->ChildActor()->Root().PreviousPosition(), player->Root().PreviousPosition());
    player->Disappear();
    scene.OnUpdate();
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(player->Ghost()->Spread().Length(), firstSpread);
    player->Appear();
    scene.OnUpdate();
    EXPECT_GT(player->Ghost()->Spread().Length(), 0.0f);
    EXPECT_LT(player->Ghost()->Spread().Length(), firstSpread);
    scene.OnUpdate();
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(player->Ghost()->AfterimageOpacity(), 0.0f);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(player->Ghost()->AfterimageOpacity(), 0.6f);
}

TEST(ActorChild, CopiesBothEndsOfThePoseForInterpolation)
{
    NS::Transform parent;
    parent.SetPosition(NS::Vector3{1.0f, 2.0f, 3.0f});
    parent.SetScale(NS::Vector3{2.0f, 3.0f, 4.0f});
    parent.SetRotation(NS::Quaternion::CreateFromYawPitchRoll(0.2f, 0.1f, 0.3f));
    parent.Snapshot();
    parent.SetPosition(NS::Vector3{5.0f, 6.0f, 7.0f});
    parent.SetScale(NS::Vector3{4.0f, 5.0f, 6.0f});
    parent.SetRotation(NS::Quaternion::CreateFromYawPitchRoll(0.8f, 0.3f, 0.1f));
    NS::Transform child;
    child.CopyFrom(parent);
    EXPECT_EQ(child.Position(), parent.Position());
    EXPECT_EQ(child.PreviousPosition(), parent.PreviousPosition());
    EXPECT_EQ(child.Rotation(), parent.Rotation());
    EXPECT_EQ(child.PreviousRotation(), parent.PreviousRotation());
    EXPECT_EQ(child.Scale(), parent.Scale());
    EXPECT_EQ(child.PreviousScale(), parent.PreviousScale());
    EXPECT_EQ(child.InterpolatedWorldMatrix(0.5f), parent.InterpolatedWorldMatrix(0.5f));
    child.SetPosition(NS::Vector3{20.0f, 0.0f, 0.0f});
    EXPECT_FLOAT_EQ(parent.Position().x, 5.0f);
}

TEST(ActorChild, ResyncsTheLiveCopiesAfterTheParentMovesLaterInTheStep)
{
    NS::Scene scene;
    PlayerActor* player = scene.SpawnActor<PlayerActor>("player");
    GL::Level::PropActor* prop = scene.SpawnActor<GL::Level::PropActor>("prop");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(prop, nullptr);
    scene.OnUpdate();
    player->Ghost()->CaptureAfterimage(player->ModelClassObj(), player->Root().WorldMatrix());
    prop->Ghost()->CaptureAfterimage(prop->ModelClassObj(), prop->Root().WorldMatrix());
    const NS::Matrix playerCapture = player->Ghost()->AfterimageWorldMatrix();
    const NS::Matrix propCapture = prop->Ghost()->AfterimageWorldMatrix();
    player->Root().ShiftPosition(NS::Vector3{10.0f, 0.0f, 0.0f});
    prop->Root().ShiftPosition(NS::Vector3{-10.0f, 0.0f, 0.0f});
    player->PrepareRender();
    prop->PrepareRender();
    EXPECT_EQ(player->ChildActor()->Root().WorldMatrix(), player->Root().WorldMatrix());
    EXPECT_EQ(player->ChildActor()->Root().PreviousPosition(), player->Root().PreviousPosition());
    EXPECT_EQ(prop->ChildActor()->Root().WorldMatrix(), prop->Root().WorldMatrix());
    EXPECT_EQ(prop->ChildActor()->Root().PreviousPosition(), prop->Root().PreviousPosition());
    EXPECT_EQ(player->Ghost()->AfterimageWorldMatrix(), playerCapture);
    EXPECT_EQ(prop->Ghost()->AfterimageWorldMatrix(), propCapture);
}
