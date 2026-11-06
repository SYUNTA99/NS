#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/SceneRenderer.h"
#include "Game/Level/TackleGhost.h"
#include "Game/PlayerActor.h"
#include "Tests/TestLogCapture.h"
#include "Tests/TestViewCamera.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Window.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/Render/RenderContext.h"

#include <gtest/gtest.h>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(!std::derived_from<NS::SceneActor, NS::IRenderable>);
static_assert(std::derived_from<GL::Level::TackleGhost, NS::SceneActor>);
static_assert(std::derived_from<GL::Level::TackleGhost, NS::IRenderable>);

namespace
{
    class SharedModelParent final : public NS::SceneActor
    {
    public:
        [[nodiscard]] GL::Level::TackleGhost* Ghost() noexcept { return m_ghost; }

    protected:
        void OnInit() override
        {
            CreateClassObj(m_model);
            NS::InitDraw(this, m_model.get(), &Root());
            m_model->SetMeshRef("cube");
            m_model->SetMaterialRef("Assets/Materials/player.mat");
            m_ghost = CreateChildActor<GL::Level::TackleGhost>();
        }

    private:
        GL::Level::TackleGhost* m_ghost = nullptr;
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

        [[nodiscard]] ChildProbe* Child() noexcept { return m_child; }
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
            m_child = CreateChildActor<ChildProbe>(m_counts);
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
        ChildProbe* m_child = nullptr;
    };

    //! 木の 1 つの節が受けた呼び出しの数
    struct NodeCounts
    {
        int appeared = 0;
        int vanished = 0;
        int steps = 0;
        int ended = 0;
        int placed = 0;
        int resolved = 0;
    };

    class TreeNode final : public NS::SceneActor
    {
    public:
        //! grandchild が空でなければ、作る時に自分の子を 1 体作る
        TreeNode(NodeCounts& counts, NodeCounts* grandchild) : m_counts(counts), m_grandchild(grandchild) {}
        NS_REFLECT_NONE(TreeNode, NS::SceneActor)

        void InitAfterPlacement() override { ++m_counts.placed; }
        void ResolveActorRefs() override { ++m_counts.resolved; }

        void OnEndPlay() override
        {
            ++m_counts.ended;
            NS::SceneActor::OnEndPlay();
        }

    protected:
        void OnInit() override
        {
            if (m_grandchild != nullptr)
            {
                ASSERT_NE(CreateChildActor<TreeNode>(*m_grandchild, nullptr), nullptr);
            }
        }

        void VisualStep() override { ++m_counts.steps; }

        void OnAppear() override
        {
            NS::SceneActor::OnAppear();
            ++m_counts.appeared;
        }

        void OnDisappear() noexcept override
        {
            ++m_counts.vanished;
            NS::SceneActor::OnDisappear();
        }

    private:
        NodeCounts& m_counts;
        NodeCounts* m_grandchild = nullptr;
    };

    //! 子を 2 体持ち、1 体目の子が孫を 1 体持つ親
    class TreeParent final : public NS::SceneActor
    {
    public:
        TreeParent(NodeCounts& first, NodeCounts& second, NodeCounts& grandchild)
            : m_first(first), m_second(second), m_grandchild(grandchild)
        {
        }
        NS_REFLECT_NONE(TreeParent, NS::SceneActor)

    protected:
        void OnInit() override
        {
            ASSERT_NE(CreateChildActor<TreeNode>(m_first, &m_grandchild), nullptr);
            ASSERT_NE(CreateChildActor<TreeNode>(m_second, nullptr), nullptr);
        }

    private:
        NodeCounts& m_first;
        NodeCounts& m_second;
        NodeCounts& m_grandchild;
    };

    int CountSelfAndDescendants(NS::SceneActor* actor)
    {
        int count = 0;
        actor->VisitSelfAndDescendants([&count](NS::SceneActor*) { ++count; });
        return count;
    }
}

TEST(ActorChild, BelongsToItsParentAndStartsAfterItWithoutAnotherScenePlacement)
{
    ChildCounts counts;
    NS::Scene scene;
    ParentProbe* parent = scene.SpawnActor<ParentProbe>("parent", counts);
    ASSERT_NE(parent, nullptr);
    ChildProbe* child = parent->Child();
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->ParentActor(), parent);
    EXPECT_EQ(child->OwningScene(), &scene);
    EXPECT_EQ(scene.Actors().ActorCount(), 1u);
    EXPECT_EQ(counts.started, 1);
    EXPECT_TRUE(counts.startedAfterParent);
    // 開始の後は子を足せない
    EXPECT_FALSE(parent->TryAnotherChild());
    EXPECT_EQ(counts.made, 1);
}

TEST(ActorChild, EveryChildAndGrandchildFollowsTheParent)
{
    NodeCounts first;
    NodeCounts second;
    NodeCounts grandchild;
    {
        NS::Scene scene;
        TreeParent* parent = scene.SpawnActor<TreeParent>("parent", first, second, grandchild);
        ASSERT_NE(parent, nullptr);
        EXPECT_EQ(CountSelfAndDescendants(parent), 4);
        EXPECT_EQ(scene.Actors().ActorCount(), 1u);
        for (const NodeCounts* node : {&first, &second, &grandchild})
        {
            EXPECT_EQ(node->placed, 1);
        }

        const int resolvedBefore = first.resolved;
        // 一覧にアクターを入れると、一覧が全部の参照を取得し直す
        ASSERT_NE(scene.SpawnActor<NS::SceneActor>("other"), nullptr);
        for (const NodeCounts* node : {&first, &second, &grandchild})
        {
            EXPECT_EQ(node->resolved, resolvedBefore + 1);
        }

        scene.OnUpdate();
        for (const NodeCounts* node : {&first, &second, &grandchild})
        {
            EXPECT_EQ(node->steps, 1);
        }

        const int appearedBefore = first.appeared;
        parent->Disappear();
        parent->Appear();
        for (const NodeCounts* node : {&first, &second, &grandchild})
        {
            EXPECT_EQ(node->vanished, 1);
            EXPECT_EQ(node->appeared, appearedBefore + 1);
        }

        parent->Destroy();
        scene.OnUpdate();
        // 残るのは参照を取得し直させるために入れた 1 体だけ
        EXPECT_EQ(scene.Actors().ActorCount(), 1u);
    }
    for (const NodeCounts* node : {&first, &second, &grandchild})
    {
        EXPECT_EQ(node->ended, 1);
    }
}

namespace
{
    //! 置く当たりの箱を持つ節。withChild なら作る時に同じ形の子を 1 体作る
    class BoxNode final : public NS::SceneActor
    {
    public:
        explicit BoxNode(bool withChild) : m_withChild(withChild) {}
        NS_REFLECT_NONE(BoxNode, NS::SceneActor)

        BoxNode* Child() noexcept { return m_child; }

    protected:
        void OnInit() override
        {
            CreateClassObj<NS::Collision, NS::BoxCollision>(m_collision);
            if (m_withChild)
            {
                m_child = CreateChildActor<BoxNode>(false);
            }
        }

    private:
        bool m_withChild = false;
        BoxNode* m_child = nullptr;
    };

    //! 当たりを持たず、当たりの箱を持つ子 (その子が孫を持つ) を 1 体持つ親
    class BoxParent final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(BoxParent, NS::SceneActor)

        BoxNode* Child() noexcept { return m_child; }

    protected:
        void OnInit() override { m_child = CreateChildActor<BoxNode>(true); }

    private:
        BoxNode* m_child = nullptr;
    };

    bool HitsDownAt(const NS::Scene& scene, float x)
    {
        float distance = 0.0f;
        return scene.Physics().Raycast({x, 2.0f, 0.0f}, -NS::Vector3::UnitY, 4.0f, distance);
    }
} // namespace

// 一覧が当たりをそろえる時、親の一覧に入っていない子と孫の当たりもそろえる
TEST(ActorChild, SyncCollisionsReachesTheChildAndGrandchild)
{
    NS::Scene scene;
    BoxParent* parent = scene.SpawnActor<BoxParent>("parent");
    ASSERT_NE(parent, nullptr);
    BoxNode* child = parent->Child();
    ASSERT_NE(child, nullptr);
    BoxNode* grandchild = child->Child();
    ASSERT_NE(grandchild, nullptr);
    ASSERT_NE(child->CollisionClassObj(), nullptr);
    ASSERT_NE(grandchild->CollisionClassObj(), nullptr);

    child->Root().SetPosition(NS::Vector3{3.0f, 0.0f, 0.0f});
    grandchild->Root().SetPosition(NS::Vector3{6.0f, 0.0f, 0.0f});
    // 根を動かしただけでは、物理シーンの箱は元の所に残る
    EXPECT_FALSE(HitsDownAt(scene, 3.0f));
    EXPECT_FALSE(HitsDownAt(scene, 6.0f));

    scene.Actors().SyncCollisions();
    EXPECT_TRUE(HitsDownAt(scene, 3.0f));
    EXPECT_TRUE(HitsDownAt(scene, 6.0f));
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
        EXPECT_FALSE(parent->Child()->IsAppeared());
        scene.OnUpdate();
        EXPECT_EQ(counts.steps, 0);
        parent->Appear();
        EXPECT_TRUE(parent->Child()->IsAppeared());
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
    ASSERT_NE(parent->Child(), nullptr);
    EXPECT_FALSE(parent->Child()->IsAppeared());
    scene.OnUpdate();
    EXPECT_EQ(counts.steps, 0);
}

TEST(ActorChild, RefusesToDestroyAChildAloneAndKeepsItWithTheParent)
{
    ChildCounts counts;
    NS::Scene scene;
    ParentProbe* parent = scene.SpawnActor<ParentProbe>("parent", counts);
    ASSERT_NE(parent, nullptr);
    ChildProbe* child = parent->Child();
    ASSERT_NE(child, nullptr);
    {
        TestLogCapture log;
        child->Destroy();
        EXPECT_EQ(log.ErrorCount(), 1);
    }
    EXPECT_FALSE(child->IsPendingDestroy());
    EXPECT_TRUE(child->IsAppeared());
    scene.OnUpdate();
    EXPECT_EQ(counts.destroyed, 0);
    EXPECT_EQ(counts.ended, 0);
    EXPECT_EQ(counts.steps, 1);
    EXPECT_EQ(CountSelfAndDescendants(parent), 2);
}

TEST(ActorChild, PlayerOwnsATackleGhostThatDrawsItsMainModel)
{
    NS::Scene scene;
    PlayerActor* player = scene.SpawnActor<PlayerActor>("player");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(player->Ghost(), nullptr);
    EXPECT_STREQ(player->Ghost()->ClassName(), "TackleGhost");
    EXPECT_EQ(player->Ghost()->ParentActor(), player);
    EXPECT_EQ(CountSelfAndDescendants(player), 2);
    EXPECT_EQ(player->Ghost()->ModelClassObj(), nullptr);
    EXPECT_EQ(scene.Actors().ActorCount(), 1u);
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
    player->Destroy();
    scene.OnUpdate();
    EXPECT_EQ(renderer->RenderableCount(), 0u);
}

TEST(ActorChild, TackleGhostDrawsTheSharedModelWithTheSideAndAfterimageCopies)
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
    GL::Level::TackleGhost* ghost = parent->Ghost();
    ASSERT_NE(ghost, nullptr);
    EXPECT_EQ(ghost->ModelClassObj(), nullptr);
    parent->Root().SetPosition(NS::Vector3{4.0f, 0.0f, 0.0f});
    ASSERT_TRUE(ghost->SetSpread(NS::Vector3{1.0f, 0.0f, 0.0f}));
    ASSERT_TRUE(ghost->SetSideOpacity(0.5f));
    ASSERT_TRUE(ghost->SetAfterimageOpacity(0.25f));
    scene.OnUpdate();
    parent->PrepareRender();
    EXPECT_EQ(ghost->Root().WorldMatrix(), parent->Root().WorldMatrix());

    NS::Gfx::RenderContext context{};
    context.groundWave.strength = 2.0f;
    std::vector<NS::Gfx::DrawItem> copies;
    ghost->Collect(context, NS::RenderSource{ghost->Root().WorldMatrix()}, copies);
    ASSERT_EQ(copies.size(), 3u);
    EXPECT_FLOAT_EQ(copies[0].constants.world._41, 5.0f);
    EXPECT_FLOAT_EQ(copies[1].constants.world._41, 3.0f);
    EXPECT_FLOAT_EQ(copies[2].constants.world._41, 0.0f);
    EXPECT_FLOAT_EQ(copies[0].constants.opacity, 0.5f);
    EXPECT_FLOAT_EQ(copies[1].constants.opacity, 0.5f);
    EXPECT_FLOAT_EQ(copies[2].constants.opacity, 0.25f);
    EXPECT_EQ(copies[0].blend, NS::Gfx::BlendMode::Alpha);
    EXPECT_FLOAT_EQ(copies[0].constants.groundWaveStrength, 0.0f);
    std::vector<NS::Gfx::DrawItem> body;
    parent->ModelClassObj()->Collect(context, NS::RenderSource{parent->Root().WorldMatrix()}, body);
    ASSERT_EQ(body.size(), 1u);
    EXPECT_EQ(copies[0].mesh, body[0].mesh);
    EXPECT_EQ(copies[2].material, body[0].material);
    EXPECT_FLOAT_EQ(body[0].constants.opacity, 1.0f);
    EXPECT_FLOAT_EQ(body[0].constants.groundWaveStrength, 2.0f);
    EXPECT_EQ(ghost->SortPriority(), -1);

    NS::SceneRenderer* renderer = parent->GetSceneRenderer();
    const std::size_t registered = renderer->RenderableCount();
    const std::size_t enabled = renderer->EnabledRenderableCount();
    ghost->Disappear();
    EXPECT_EQ(renderer->EnabledRenderableCount(), enabled - 1u);
    EXPECT_TRUE(parent->IsAppeared());
    ghost->Appear();
    EXPECT_EQ(renderer->EnabledRenderableCount(), enabled);
    ghost->OnEndPlay();
    copies.clear();
    ghost->Collect(context, NS::RenderSource{ghost->Root().WorldMatrix()}, copies);
    EXPECT_TRUE(copies.empty());
    EXPECT_EQ(renderer->RenderableCount(), registered - 1u);
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
    player->PrepareRender();
    EXPECT_EQ(player->Ghost()->Root().Position(), player->Root().Position());
    EXPECT_EQ(player->Ghost()->Root().PreviousPosition(), player->Root().PreviousPosition());
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
    ASSERT_NE(player, nullptr);
    scene.OnUpdate();
    player->Ghost()->CaptureAfterimage(player->ModelClassObj(), player->Root().WorldMatrix());
    const NS::Matrix playerCapture = player->Ghost()->AfterimageWorldMatrix();
    player->Root().ShiftPosition(NS::Vector3{10.0f, 0.0f, 0.0f});
    player->PrepareRender();
    EXPECT_EQ(player->Ghost()->Root().WorldMatrix(), player->Root().WorldMatrix());
    EXPECT_EQ(player->Ghost()->Root().PreviousPosition(), player->Root().PreviousPosition());
    EXPECT_EQ(player->Ghost()->AfterimageWorldMatrix(), playerCapture);
}
