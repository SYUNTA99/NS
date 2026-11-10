#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/SceneRenderer.h"
#include "Game/Level/TackleGhost.h"
#include "Game/PlayerActor.h"
#include "Tests/TestViewCamera.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Window.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include "TestSpawn.h"

#include <gtest/gtest.h>

#include <concepts>
#include <vector>

// エアダッシュの残像は自機の部品で、左右と明けの写しの列を自機の Model に書く。子のアクターではない
static_assert(std::derived_from<GL::Level::TackleGhost, NS::Object>);
static_assert(!std::derived_from<GL::Level::TackleGhost, NS::SceneActor>);

namespace
{
    // 箱の Model と残像を持ち、毎ステップ残像に自分の Model へ写しを書かせる
    class GhostOwnerActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(GhostOwnerActor, NS::SceneActor)

        GL::Level::TackleGhost ghost;

    protected:
        void OnInit() override
        {
            CreateClassObj(m_model);
            m_model->SetMeshRef("cube");
            m_model->SetMaterialRef("Assets/Materials/player.mat");
        }
        void VisualStep() override { ghost.Step(this, m_model.get()); }
    };
} // namespace

NS_CLASS(GhostOwnerActor)

TEST(TackleGhost, PlayerOwnsTheGhostAndItWritesTheCopiesToThePlayersModel)
{
    NS::Scene scene;
    PlayerActor* player = SpawnFromActorFile<PlayerActor>(scene, "PlayerActor", "player");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(player->Ghost(), nullptr);
    EXPECT_STREQ(player->Ghost()->ClassName(), "TackleGhost");
    EXPECT_EQ(player->Ghost()->SortPriority(), -1);
    EXPECT_TRUE(player->ModelClassObj()->Copies().empty());
    NS::SceneRenderer* renderer = player->GetSceneRenderer();
    ASSERT_NE(renderer, nullptr);
    // 写しは自機の Model の値なので、描く仕組みの組は自機の Model の 1 つだけ
    EXPECT_EQ(renderer->ModelCount(), 1u);

    ASSERT_TRUE(player->Ghost()->SetSpread(NS::Vector3{0.3f, 0.0f, 0.0f}));
    ASSERT_TRUE(player->Ghost()->SetAfterimageOpacity(0.4f));
    scene.OnUpdate();
    EXPECT_EQ(player->ModelClassObj()->Copies().size(), 3u);
    EXPECT_EQ(player->ModelClassObj()->CopyPriority(), -1);

    player->Disappear();
    player->Appear();
    EXPECT_FLOAT_EQ(player->Ghost()->Spread().x, 0.3f);
    EXPECT_FLOAT_EQ(player->Ghost()->AfterimageOpacity(), 0.4f);

    // やり直しで残像を止めると、自機の Model の写しも消える
    player->Ghost()->Stop(player->ModelClassObj());
    EXPECT_TRUE(player->ModelClassObj()->Copies().empty());
    player->Destroy();
    scene.OnUpdate();
    EXPECT_EQ(renderer->ModelCount(), 0u);
}

TEST(TackleGhost, CopiesDrawTheOwnersModelAtTheSideAndAfterimagePlaces)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "残像の写しの試し", .size = {64, 36}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AssetManagerScope useAssets{assets};
    NS::Scene scene;
    scene.GetSceneRenderer()->SetDevice(&device);
    GhostOwnerActor* owner = SpawnFromActorFile<GhostOwnerActor>(scene, "GhostOwnerActor", "owner");
    ASSERT_NE(owner, nullptr);
    NS::Model* model = owner->ModelClassObj();
    ASSERT_NE(model, nullptr);
    owner->Root().SetPosition(NS::Vector3{4.0f, 0.0f, 0.0f});
    ASSERT_TRUE(owner->ghost.SetSpread(NS::Vector3{1.0f, 0.0f, 0.0f}));
    ASSERT_TRUE(owner->ghost.SetSideOpacity(0.5f));
    ASSERT_TRUE(owner->ghost.SetAfterimageOpacity(0.25f));
    scene.OnUpdate();

    NS::Gfx::RenderContext context{};
    std::vector<NS::Gfx::DrawItem> drawn;
    model->CollectCopies(context, owner->Root().WorldMatrix(), drawn);
    ASSERT_EQ(drawn.size(), 3u);
    EXPECT_FLOAT_EQ(drawn[0].constants.world._41, 5.0f);
    EXPECT_FLOAT_EQ(drawn[1].constants.world._41, 3.0f);
    EXPECT_FLOAT_EQ(drawn[2].constants.world._41, 0.0f);
    EXPECT_FLOAT_EQ(drawn[0].constants.opacity, 0.5f);
    EXPECT_FLOAT_EQ(drawn[1].constants.opacity, 0.5f);
    EXPECT_FLOAT_EQ(drawn[2].constants.opacity, 0.25f);
    EXPECT_EQ(drawn[0].blend, NS::Gfx::BlendMode::Alpha);
    EXPECT_FLOAT_EQ(drawn[0].constants.sceneValuesWeight, 0.0f);
    std::vector<NS::Gfx::DrawItem> body;
    model->Collect(context, owner->Root().WorldMatrix(), body);
    ASSERT_EQ(body.size(), 1u);
    EXPECT_EQ(drawn[0].mesh, body[0].mesh);
    EXPECT_EQ(drawn[2].material, body[0].material);
    EXPECT_FLOAT_EQ(body[0].constants.opacity, 1.0f);
    EXPECT_FLOAT_EQ(body[0].constants.sceneValuesWeight, 1.0f);
    EXPECT_EQ(model->CopyPriority(), -1);

    // 左右の写しは描く時に補間した根から作るので、割合 0.5 では前と今の根の真ん中に描く。明けの写しは控えた所に残る
    owner->Root().Snapshot();
    owner->Root().SetPosition(NS::Vector3{8.0f, 0.0f, 0.0f});
    context.alpha = 0.5f;
    drawn.clear();
    model->CollectCopies(context, owner->Root().InterpolatedWorldMatrix(0.5f), drawn);
    ASSERT_EQ(drawn.size(), 3u);
    EXPECT_FLOAT_EQ(drawn[0].constants.world._41, 7.0f);
    EXPECT_FLOAT_EQ(drawn[1].constants.world._41, 5.0f);
    EXPECT_FLOAT_EQ(drawn[2].constants.world._41, 0.0f);
}

TEST(TackleGhost, PausesWhileThePlayerIsHiddenAndContinuesAfterReappearing)
{
    NS::Scene scene;
    PlayerActor* player = SpawnFromActorFile<PlayerActor>(scene, "PlayerActor", "player");
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
    player->Ghost()->Start(player, run);
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

TEST(TackleGhost, TheAfterimageStaysWhereItWasCapturedWhenTheRootMoves)
{
    NS::Scene scene;
    PlayerActor* player = SpawnFromActorFile<PlayerActor>(scene, "PlayerActor", "player");
    ASSERT_NE(player, nullptr);
    scene.OnUpdate();
    player->Ghost()->CaptureAfterimage(player->ModelClassObj(), player->Root().WorldMatrix());
    const NS::Matrix captured = player->Ghost()->AfterimageWorldMatrix();
    player->Root().ShiftPosition(NS::Vector3{10.0f, 0.0f, 0.0f});
    EXPECT_EQ(player->Ghost()->AfterimageWorldMatrix(), captured);
}
