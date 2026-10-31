#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/SceneRenderer.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

namespace
{
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

    class ChildProbe final : public NS::SceneActor
    {
    public:
        explicit ChildProbe(ChildCounts& counts) : m_counts(counts) { ++m_counts.made; }
        ~ChildProbe() override { ++m_counts.destroyed; }
        NS_REFLECT_NONE(ChildProbe, NS::SceneActor)

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
            NS::SceneActor::OnEndPlay();
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
            NS::SceneActor::OnDisappear();
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
    NS::SceneActor* child = parent->ChildActor();
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
    EXPECT_EQ(player->ChildActor(), player->Ghost());
    EXPECT_EQ(prop->ChildActor(), prop->Ghost());
    EXPECT_EQ(player->Ghost()->ParentActor(), player);
    EXPECT_EQ(prop->Ghost()->ParentActor(), prop);
    EXPECT_EQ(player->Ghost()->ModelClassObj(), nullptr);
    EXPECT_EQ(prop->Ghost()->ModelClassObj(), nullptr);
    EXPECT_EQ(scene.Actors().ActorCount(), 2u);
    NS::SceneRenderer* renderer = player->GetSceneRenderer();
    ASSERT_NE(renderer, nullptr);
    const std::size_t registered = renderer->RenderableCount();
    ASSERT_TRUE(player->Ghost()->SetSpread(NS::Vector3{0.3f, 0.0f, 0.0f}));
    ASSERT_TRUE(player->Ghost()->SetAfterimageOpacity(0.4f));
    player->Disappear();
    EXPECT_FALSE(player->Ghost()->IsAppeared());
    EXPECT_EQ(renderer->RenderableCount(), registered);
    player->Appear();
    EXPECT_TRUE(player->Ghost()->IsAppeared());
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
    player->Ghost()->Start(run);
    scene.OnUpdate();
    const float firstSpread = player->Ghost()->Spread().Length();
    ASSERT_GT(firstSpread, 0.0f);
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
