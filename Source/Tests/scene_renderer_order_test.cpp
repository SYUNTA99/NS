#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/SceneRenderer.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Window.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include "NSlib/Phys/PhysicsScene.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

// 描く仕組みは Model・Shadow・写しを 1 つの半透明の列に混ぜ、遠い順・優先度の小さい順・登録の順に描く

namespace
{
    // 半透明のマテリアルで箱を描く
    class GlassActor final : public NS::SceneActor
    {
    public:
        [[nodiscard]] NS::Model* Body() const noexcept { return m_model.get(); }

    protected:
        void OnInit() override
        {
            NS::Model* body = CreateClassObj(m_model);
            body->SetMeshRef("cube");
            body->SetMaterialRef("Assets/Materials/shadow.mat");
        }
    };

    // 真下の地面に影を描く
    class ShadowOnlyActor final : public NS::SceneActor
    {
    protected:
        void OnInit() override
        {
            CreateClassObj(m_shadow);
        }
    };

    // 不透明の箱を描き、Copy の後は同じ箱を半透明の写しとしても描く
    class CopyActor final : public NS::SceneActor
    {
    public:
        void Copy(int priority)
        {
            const std::array<NS::ModelDraw, 1> copies{NS::ModelDraw{.offset = NS::Vector3{}, .opacity = 0.5f}};
            m_model->SetCopies(copies, priority);
        }

    protected:
        void OnInit() override
        {
            NS::Model* body = CreateClassObj(m_model);
            body->SetMeshRef("cube");
            body->SetMaterialRef("Assets/Materials/player.mat");
        }
    };

    // 原点から +z を見るカメラ。並べる物は全部視錐台に入る
    NS::Gfx::RenderContext LookAlongZ()
    {
        NS::Gfx::RenderContext context{};
        context.alpha = 1.0f;
        context.cameraPosition = NS::Vector3{};
        context.viewProjection =
            NS::Matrix::CreateLookAt(NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f}, NS::Vector3{0.0f, 1.0f, 0.0f}) *
            NS::Matrix::CreatePerspectiveFieldOfView(1.5f, 1.0f, 0.1f, 100.0f);
        return context;
    }

    // 描いた物の位置の x と z
    struct Spot
    {
        float x = 0.0f;
        float z = 0.0f;
    };

    std::vector<Spot> SpotsOf(const std::vector<NS::Gfx::DrawItem>& items)
    {
        std::vector<Spot> spots;
        for (const NS::Gfx::DrawItem& item : items)
        {
            spots.push_back(Spot{item.constants.world._41, item.constants.world._43});
        }
        return spots;
    }

    class SceneRendererOrder : public ::testing::Test
    {
    protected:
        NS::OS::Window window{NS::OS::WindowDesc{.title = "描く順の試し", .size = {64, 36}, .visible = false}};
        NS::Gfx::D3DDevice device{NS::Gfx::D3DDeviceDesc{}, window};
        NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
        NS::AssetManagerScope useAssets{assets};
        NS::Scene scene;

        void SetUp() override
        {
            ASSERT_TRUE(device.IsValid());
            scene.GetSceneRenderer()->SetDevice(&device);
            // 上面が y = 0 の地面。影はここに落ちる
            scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, -0.5f, 10.0f},
                                               NS::Quaternion::Identity,
                                               NS::Vector3{20.0f, 0.5f, 20.0f}),
                                   NS::Phys::k_TerrainObjectLayer);
        }

        template <typename T>
        T* SpawnAt(const NS::Vector3& position)
        {
            T* actor = scene.SpawnActor<T>("actor");
            actor->Root().SetPosition(position);
            return actor;
        }
    };
} // namespace

TEST_F(SceneRendererOrder, TransparentPassMixesModelsShadowsAndCopiesFarFirstThenPriorityThenRegistration)
{
    // 登録の順は、並べ替えの結果と違う順にする
    GlassActor* nearA = SpawnAt<GlassActor>(NS::Vector3{3.0f, 0.0f, 4.0f});
    (void)SpawnAt<ShadowOnlyActor>(NS::Vector3{0.0f, 1.0f, 10.0f});
    CopyActor* nearCopy = SpawnAt<CopyActor>(NS::Vector3{0.0f, 0.0f, 5.0f});
    (void)SpawnAt<GlassActor>(NS::Vector3{-3.0f, 0.0f, 4.0f});
    CopyActor* farCopy = SpawnAt<CopyActor>(NS::Vector3{0.0f, 0.0f, 15.0f});
    (void)SpawnAt<GlassActor>(NS::Vector3{0.0f, 0.0f, 20.0f});
    // 近い写しは箱と同じ距離で、優先度が箱 (0) より小さい
    nearCopy->Copy(-1);
    farCopy->Copy(0);
    scene.OnUpdate(); // 影の地面を探す
    // 消して出し直しても、登録の順は変わらない
    nearA->Disappear();
    nearA->Appear();

    std::vector<NS::Gfx::DrawItem> items;
    scene.GetSceneRenderer()->CollectPass(LookAlongZ(), NS::Gfx::RenderPass::Transparent, items);

    const std::vector<Spot> spots = SpotsOf(items);
    ASSERT_EQ(spots.size(), 6u);
    EXPECT_FLOAT_EQ(spots[0].z, 20.0f); // 一番遠い箱
    EXPECT_FLOAT_EQ(spots[1].z, 15.0f); // 遠い写し
    EXPECT_FLOAT_EQ(spots[2].z, 10.0f); // 影
    EXPECT_FLOAT_EQ(spots[3].z, 5.0f);  // 箱と同じ距離で優先度の小さい写し
    EXPECT_FLOAT_EQ(spots[4].x, 3.0f);  // 同じ距離と優先度の 2 つの箱は登録の順
    EXPECT_FLOAT_EQ(spots[5].x, -3.0f);
}

TEST_F(SceneRendererOrder, CollectsOnlyTheRequestedPassInsideTheView)
{
    (void)SpawnAt<GlassActor>(NS::Vector3{0.0f, 0.0f, 6.0f});
    (void)SpawnAt<GlassActor>(NS::Vector3{0.0f, 0.0f, -6.0f}); // カメラの後ろ
    (void)SpawnAt<CopyActor>(NS::Vector3{1.0f, 0.0f, 8.0f}); // 写しを書かないので不透明の箱だけを描く
    scene.OnUpdate();
    const NS::Gfx::RenderContext context = LookAlongZ();

    std::vector<NS::Gfx::DrawItem> transparent;
    scene.GetSceneRenderer()->CollectPass(context, NS::Gfx::RenderPass::Transparent, transparent);
    std::vector<NS::Gfx::DrawItem> opaqueItems;
    scene.GetSceneRenderer()->CollectPass(context, NS::Gfx::RenderPass::Opaque, opaqueItems);
    std::vector<NS::Gfx::DrawItem> afterSky;
    scene.GetSceneRenderer()->CollectPass(context, NS::Gfx::RenderPass::AfterSky, afterSky);

    const std::vector<Spot> glass = SpotsOf(transparent);
    ASSERT_EQ(glass.size(), 1u);
    EXPECT_FLOAT_EQ(glass[0].z, 6.0f);
    const std::vector<Spot> box = SpotsOf(opaqueItems);
    ASSERT_EQ(box.size(), 1u);
    EXPECT_FLOAT_EQ(box[0].x, 1.0f);
    EXPECT_TRUE(afterSky.empty());
}

TEST_F(SceneRendererOrder, HiddenEntriesAreNotDrawnAndACopyingModelDrawsItsBodyAndCopies)
{
    (void)SpawnAt<GlassActor>(NS::Vector3{0.0f, 0.0f, 6.0f});
    GlassActor* hidden = SpawnAt<GlassActor>(NS::Vector3{0.0f, 0.0f, 8.0f});
    CopyActor* copier = SpawnAt<CopyActor>(NS::Vector3{2.0f, 0.0f, 6.0f});
    copier->Copy(0);
    scene.OnUpdate();
    hidden->Disappear();
    const NS::Gfx::RenderContext context = LookAlongZ();

    std::vector<NS::Gfx::DrawItem> transparent;
    scene.GetSceneRenderer()->CollectPass(context, NS::Gfx::RenderPass::Transparent, transparent);
    std::vector<NS::Gfx::DrawItem> opaque;
    scene.GetSceneRenderer()->CollectPass(context, NS::Gfx::RenderPass::Opaque, opaque);

    // 隠した箱は描かず、写しを持つ Model は自分の不透明の箱と、同じ箱の半透明の写しの両方を描く
    const std::vector<Spot> spots = SpotsOf(transparent);
    ASSERT_EQ(spots.size(), 2u);
    EXPECT_FLOAT_EQ(spots[0].x, 2.0f); // 写しの方が少し遠い
    EXPECT_FLOAT_EQ(spots[1].x, 0.0f);
    const std::vector<Spot> body = SpotsOf(opaque);
    ASSERT_EQ(body.size(), 1u);
    EXPECT_FLOAT_EQ(body[0].x, 2.0f);
    EXPECT_EQ(scene.GetSceneRenderer()->ModelCount(), 3u);
    EXPECT_EQ(scene.GetSceneRenderer()->EnabledModelCount(), 2u);
}
