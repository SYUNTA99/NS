#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Phys/PhysicsScene.h"

#include <gtest/gtest.h>

#include <optional>

namespace
{
    class ShadowActor final : public NS::SceneActor
    {
    public:
        [[nodiscard]] NS::Shadow* Ground() const noexcept { return m_shadow.get(); }

    protected:
        void OnInit() override { CreateClassObj(m_shadow); }
    };

    // 自分の更新で、先に更新を終えた別のアクターを動かす
    class MoverActor final : public NS::SceneActor
    {
    public:
        NS::SceneActor* target = nullptr;
        NS::Vector3 destination{};

    protected:
        void VisualStep() override
        {
            if (target != nullptr)
            {
                target->Root().SetPosition(destination);
            }
        }
    };

    // 上面が y = 0 の地面を置く
    void AddGround(NS::Scene& scene)
    {
        scene.Physics().AddBox(
            NS::MakeOBB(NS::Vector3{0.0f, -0.5f, 0.0f}, NS::Quaternion::Identity, NS::Vector3{10.0f, 0.5f, 10.0f}),
            NS::Phys::k_TerrainObjectLayer);
    }
} // namespace

TEST(ShadowGround, UpdateKeepsTheGroundHeight)
{
    NS::Scene scene;
    AddGround(scene);
    NS::Shadow shadow;

    shadow.Update(&scene, NS::Vector3{0.0f, 3.0f, 0.0f});

    ASSERT_TRUE(shadow.HasGround());
    EXPECT_NEAR(shadow.GroundHeight(), 0.0f, 1e-3f);
}

TEST(ShadowGround, DrawsOnTheKeptGroundHeightWithoutAskingTheGroundAgain)
{
    // 描く時に当たりを調べると、影が当たりのシーンを覚える事になる。地面の高さは固定更新で探した値を使う
    // 補間した根は今の根より遅れて描かれるので、距離を持って根から引くと、落ちている間に影が地面より下へずれる
    NS::Scene scene;
    AddGround(scene);
    NS::Shadow shadow;
    shadow.Update(&scene, NS::Vector3{0.0f, 3.0f, 0.0f});

    const std::optional<NS::Matrix> world = shadow.DrawWorldMatrix(NS::Vector3{0.0f, 4.0f, 0.0f});

    ASSERT_TRUE(world.has_value());
    EXPECT_NEAR(world->Translation().y, 0.02f, 1e-3f); // 地面の高さ 0 + 表面オフセット
}

TEST(ShadowGround, HalfAlphaDrawsHalfWayBetweenTheRoots)
{
    NS::Scene scene;
    AddGround(scene);
    NS::Shadow shadow;
    NS::Transform root;
    root.SetPosition(NS::Vector3{0.0f, 2.0f, 0.0f});
    root.Snapshot();
    root.SetPosition(NS::Vector3{4.0f, 2.0f, 0.0f});
    shadow.Update(&scene, root.Position());

    const std::optional<NS::Matrix> world = shadow.DrawWorldMatrix(root.InterpolatedWorldMatrix(0.5f).Translation());

    ASSERT_TRUE(world.has_value());
    EXPECT_NEAR(world->Translation().x, 2.0f, 1e-4f);
}

TEST(ShadowGround, TheStepFindsTheGroundAndAStoppedWorldStillFollowsEdits)
{
    NS::Scene scene;
    AddGround(scene);
    ShadowActor* actor = scene.SpawnActor<ShadowActor>("ShadowActor");
    actor->Root().SetPosition(NS::Vector3{0.0f, 3.0f, 0.0f});

    scene.OnUpdate();
    ASSERT_TRUE(actor->Ground()->HasGround());
    EXPECT_NEAR(actor->Ground()->GroundHeight(), 0.0f, 1e-3f);

    // エディタで止めている間はアクターの Update が回らない。地面の外へ動かした影も探し直す
    scene.SetSimulationEnabled(false);
    actor->Root().SetPosition(NS::Vector3{30.0f, 3.0f, 0.0f});
    scene.OnUpdate();
    EXPECT_FALSE(actor->Ground()->HasGround());
}

TEST(ShadowGround, FindsTheGroundAfterLaterActorsMovedIt)
{
    // 影を持つアクターの更新の後で別のアクターが動かしても、その歩の最後の位置の地面を探す
    NS::Scene scene;
    AddGround(scene);
    ShadowActor* actor = scene.SpawnActor<ShadowActor>("ShadowActor");
    actor->Root().SetPosition(NS::Vector3{0.0f, 3.0f, 0.0f});
    MoverActor* mover = scene.SpawnActor<MoverActor>("MoverActor");
    mover->target = actor;
    mover->destination = NS::Vector3{30.0f, 3.0f, 0.0f};

    scene.OnUpdate();

    EXPECT_FALSE(actor->Ground()->HasGround());
}

TEST(ShadowGround, AppearingFindsTheGroundWhereItAppears)
{
    // 歩の外で出した物は、歩の最後の探し直しより先に描かれる。消えた所の地面を描かない
    NS::Scene scene;
    AddGround(scene);
    ShadowActor* actor = scene.SpawnActor<ShadowActor>("ShadowActor");
    actor->Root().SetPosition(NS::Vector3{30.0f, 3.0f, 0.0f});
    scene.OnUpdate();
    ASSERT_FALSE(actor->Ground()->HasGround());

    actor->Disappear();
    actor->Root().SetPosition(NS::Vector3{0.0f, 3.0f, 0.0f});
    actor->Appear();

    EXPECT_TRUE(actor->Ground()->HasGround());
}
