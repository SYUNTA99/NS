#include "Game/Level/TackleGhost.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/ModelShake.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

// 止めの間の横揺れは、体ごと画面の横へ決めたフレーム数ごとに左右を入れ替えて揺らし、止めの終わりで 0 になる
// 揺らすのは描く形だけで、根の位置と当たりは動かさない

namespace
{
    class ModelActor final : public NS::SceneActor
    {
    public:
        GL::Level::TackleGhost ghost{this};

    protected:
        void OnInit() override { CreateClassObj(m_model, this); }
    };

    NS::DrawShakeDesc SideShakeOf(int length)
    {
        return NS::DrawShakeDesc{.shape = NS::DrawShakeShape::Side,
                                 .axis = NS::Vector3{1.0f, 0.0f, 0.0f},
                                 .amplitude = 0.2f,
                                 .length = length,
                                 .seed = 7u,
                                 .firstSign = 1.0f,
                                 .flipFrames = 1};
    }
} // namespace

// 振れ幅 × (1 − 経過 ÷ 長さ) に 0.7〜1 のばらつきを掛ける。1 フレーム目から入れ替えのフレーム数 (2) の間は最初の向き、
// そこから 2 フレームごとに入れ替わる。止めの後半まで揺れが残るよう、弱まり方は直線
TEST(BodyShake, OffsetHoldsEachSideForTheFlipFramesAndFallsLinearly)
{
    constexpr int k_Length = 12;
    constexpr int k_Flip = 2;
    for (int frame = 1; frame < k_Length; ++frame)
    {
        SCOPED_TRACE(frame);
        const float offset = NS::BodyShakeOffset(frame, k_Length, 0.1f, 7u, -1.0f, k_Flip);
        const float envelope = 0.1f * (1.0f - static_cast<float>(frame) / static_cast<float>(k_Length));
        EXPECT_GE(std::abs(offset), envelope * 0.7f - 1.0e-6f);
        EXPECT_LE(std::abs(offset), envelope + 1.0e-6f);
        // 1・2 フレーム目は最初の向き (−)、3・4 は +、5・6 は −
        float sign = -1.0f;
        if (((frame - 1) / k_Flip) % 2 == 1)
        {
            sign = 1.0f;
        }
        EXPECT_GT(offset * sign, 0.0f);
    }
    EXPECT_FLOAT_EQ(NS::BodyShakeOffset(k_Length, k_Length, 0.1f, 7u, -1.0f, k_Flip), 0.0f);
    EXPECT_FLOAT_EQ(NS::BodyShakeOffset(0, k_Length, 0.1f, 7u, -1.0f, k_Flip), 0.0f);
    EXPECT_FLOAT_EQ(NS::BodyShakeOffset(3, 0, 0.1f, 7u, -1.0f, k_Flip), 0.0f);
    // 入れ替えのフレーム数が 1 未満なら 1 フレームごとに入れ替える
    EXPECT_LT(NS::BodyShakeOffset(1, k_Length, 0.1f, 7u, 1.0f, 0) * NS::BodyShakeOffset(2, k_Length, 0.1f, 7u, 1.0f, 0),
              0.0f);
    // 同じ種は同じ揺れ、違う種はばらつきが違う
    EXPECT_FLOAT_EQ(NS::BodyShakeOffset(2, k_Length, 0.1f, 7u, 1.0f, k_Flip),
                    NS::BodyShakeOffset(2, k_Length, 0.1f, 7u, 1.0f, k_Flip));
    bool differs = false;
    for (int frame = 1; frame < k_Length; ++frame)
    {
        differs = differs || NS::BodyShakeOffset(frame, k_Length, 0.1f, 7u, 1.0f, k_Flip) !=
                                 NS::BodyShakeOffset(frame, k_Length, 0.1f, 8u, 1.0f, k_Flip);
    }
    EXPECT_TRUE(differs);
}

// 揺れのずれは描く行列の位置にだけ足し、根の位置は変えない。止めると元の位置で描く
// 描く箱へ足す分は、試しの世界にメッシュが無いので見ない
TEST(BodyShake, DrawOffsetMovesOnlyTheDrawnShape)
{
    NS::Scene scene;
    ModelActor* actor = scene.SpawnTransient<ModelActor>();
    actor->Root().SetPosition(NS::Vector3{2.0f, 1.0f, 0.0f});
    NS::Model* model = actor->ModelClassObj();
    ASSERT_NE(model, nullptr);
    const NS::Matrix before = model->DrawWorldMatrix(1.0f);

    const NS::DrawShakeDesc shake = SideShakeOf(6);
    model->StartDrawShake(shake);
    model->AdvanceDraw();
    const NS::Vector3 offset = shake.OffsetAt(1);
    ASSERT_NE(offset.x, 0.0f);
    const NS::Matrix after = model->DrawWorldMatrix(1.0f);
    EXPECT_NEAR(after._41 - before._41, offset.x, 1.0e-5f);
    EXPECT_NEAR(after._42 - before._42, 0.0f, 1.0e-5f);
    EXPECT_FLOAT_EQ(actor->Root().Position().x, 2.0f);

    model->StopDrawShake();
    const NS::Matrix reset = model->DrawWorldMatrix(1.0f);
    EXPECT_FLOAT_EQ(reset._41, before._41);
}

// 何も頼んでいない Model は、ずれ 0・倍率 1・震えなしで描く
TEST(BodyShake, ModelWithoutRequestsDrawsUnadjusted)
{
    NS::Scene scene;
    ModelActor* actor = scene.SpawnTransient<ModelActor>();
    NS::Model* model = actor->ModelClassObj();
    ASSERT_NE(model, nullptr);
    model->AdvanceDraw();
    EXPECT_TRUE(model->GetDrawOffset() == (NS::Vector3{0.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(model->GetDrawScale() == (NS::Vector3{1.0f, 1.0f, 1.0f}));
    EXPECT_FLOAT_EQ(model->GetDrawTremor().amplitude, 0.0f);
}

// 揺れは頼んだ後の最初の進めを 1 フレーム目に数える。横揺れは長さのフレームで 0 になり、その次で止まる
TEST(BodyShake, ShakeStartsOnTheNextAdvanceAndEndsAfterTheLength)
{
    NS::Scene scene;
    ModelActor* actor = scene.SpawnTransient<ModelActor>();
    NS::Model* model = actor->ModelClassObj();
    ASSERT_NE(model, nullptr);
    constexpr int k_Length = 4;
    const NS::DrawShakeDesc shake = SideShakeOf(k_Length);
    model->StartDrawShake(shake);
    EXPECT_FLOAT_EQ(model->GetDrawOffset().x, 0.0f);
    for (int frame = 1; frame <= k_Length + 2; ++frame)
    {
        SCOPED_TRACE(frame);
        model->AdvanceDraw();
        EXPECT_FLOAT_EQ(model->GetDrawOffset().x, shake.OffsetAt(frame).x);
    }
    EXPECT_FLOAT_EQ(model->GetDrawOffset().x, 0.0f);

    // 奥揺れは長さのフレームまで揺れ、長さの次で 0 になる
    NS::DrawShakeDesc depth = shake;
    depth.shape = NS::DrawShakeShape::Depth;
    depth.pushFrames = 2;
    depth.returnRatio = 0.5f;
    model->StartDrawShake(depth);
    for (int frame = 1; frame <= k_Length; ++frame)
    {
        model->AdvanceDraw();
    }
    EXPECT_GT(model->GetDrawOffset().x, 0.0f);
    model->AdvanceDraw();
    EXPECT_FLOAT_EQ(model->GetDrawOffset().x, 0.0f);
}

// 震えは頼んだ後の最初の進めを 0 フレーム目に数え、長さのフレームで振れ幅 0 を書いて止まる
TEST(BodyShake, TremorStartsAtFrameZeroAndEndsAtTheLength)
{
    NS::Scene scene;
    ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{0.0f, 0.0f, -5.0f}, NS::Vector3{}), nullptr);
    ModelActor* actor = scene.SpawnTransient<ModelActor>();
    NS::Model* model = actor->ModelClassObj();
    ASSERT_NE(model, nullptr);
    constexpr int k_Length = 3;
    model->StartDrawTremor(NS::DrawTremorDesc{
        .contactOffset = NS::Vector3{0.0f, 0.5f, 0.0f}, .amplitudePixels = 4.0f, .length = k_Length, .bodyLength = 1.0f});
    EXPECT_FLOAT_EQ(model->GetDrawTremor().amplitude, 0.0f);
    for (int frame = 0; frame < k_Length; ++frame)
    {
        SCOPED_TRACE(frame);
        model->AdvanceDraw();
        EXPECT_GT(model->GetDrawTremor().amplitude, 0.0f);
        EXPECT_FLOAT_EQ(model->GetDrawTremor().elapsedFrames, static_cast<float>(frame));
        EXPECT_FLOAT_EQ(model->GetDrawTremor().contactOffset.y, 0.5f);
    }
    model->AdvanceDraw();
    EXPECT_FLOAT_EQ(model->GetDrawTremor().amplitude, 0.0f);

    model->StartDrawTremor(NS::DrawTremorDesc{.amplitudePixels = 4.0f, .length = k_Length, .bodyLength = 1.0f});
    model->AdvanceDraw();
    ASSERT_GT(model->GetDrawTremor().amplitude, 0.0f);
    model->StopDrawTremor();
    EXPECT_FLOAT_EQ(model->GetDrawTremor().amplitude, 0.0f);
}

// 描く倍率は固定フレームの頭の値から補間し、Snap は補間しない。有限の正でない倍率は断って前の値を残す
TEST(BodyShake, DrawScaleInterpolatesAndSnapDoesNot)
{
    NS::Scene scene;
    ModelActor* actor = scene.SpawnTransient<ModelActor>();
    NS::Model* model = actor->ModelClassObj();
    ASSERT_NE(model, nullptr);
    model->Snapshot();
    ASSERT_TRUE(model->SetDrawScale(NS::Vector3{1.0f, 0.5f, 1.0f}));
    EXPECT_NEAR(model->DrawWorldMatrix(0.0f)._22, 1.0f, 1.0e-5f);
    EXPECT_NEAR(model->DrawWorldMatrix(1.0f)._22, 0.5f, 1.0e-5f);

    ASSERT_TRUE(model->SnapDrawScale(NS::Vector3{1.0f, 2.0f, 1.0f}));
    EXPECT_NEAR(model->DrawWorldMatrix(0.0f)._22, 2.0f, 1.0e-5f);

    EXPECT_FALSE(model->SetDrawScale(NS::Vector3{1.0f, 0.0f, 1.0f}));
    EXPECT_FALSE(model->SnapDrawScale(NS::Vector3{1.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f}));
    EXPECT_FLOAT_EQ(model->GetDrawScale().y, 2.0f);
}

// 残像は根の形を ±離れだけずらした所に描く。描く時だけのずれは残像に足さないので、体が片側へ振れても残像は
// 根を挟んで左右に残る。有限でない離れは書かない
TEST(BodyShake, GhostsSitAtPlusAndMinusTheSpreadAroundTheRoot)
{
    NS::Scene scene;
    ModelActor* actor = scene.SpawnTransient<ModelActor>();
    actor->Root().SetPosition(NS::Vector3{2.0f, 1.0f, 0.0f});
    NS::Model* model = actor->ModelClassObj();
    ASSERT_NE(model, nullptr);
    const NS::Matrix root = model->DrawWorldMatrix(1.0f);
    model->StartDrawShake(SideShakeOf(6));
    model->AdvanceDraw();
    ASSERT_NE(model->GetDrawOffset().x, 0.0f);
    ASSERT_TRUE(actor->ghost.SetSpread(NS::Vector3{0.3f, 0.0f, -0.2f}));
    const NS::Matrix plus = actor->ghost.SideWorldMatrix(1.0f, 1.0f);
    const NS::Matrix minus = actor->ghost.SideWorldMatrix(1.0f, -1.0f);
    EXPECT_NEAR(plus._41 - root._41, 0.3f, 1.0e-5f);
    EXPECT_NEAR(plus._43 - root._43, -0.2f, 1.0e-5f);
    EXPECT_NEAR(minus._41 - root._41, -0.3f, 1.0e-5f);
    EXPECT_NEAR(minus._43 - root._43, 0.2f, 1.0e-5f);
    EXPECT_FALSE(actor->ghost.SetSpread(NS::Vector3{std::numeric_limits<float>::infinity(), 0.0f, 0.0f}));
    EXPECT_FLOAT_EQ(actor->ghost.Spread().x, 0.3f);
}

// 奥揺れは押し直すたびに弱まりながら押しては戻し、止めの最後のフレームまで揺れる
TEST(BodyShake, DepthOffsetPushesEveryPushFramesAndReturnsBetween)
{
    constexpr int k_Length = 12;
    constexpr int k_Push = 4;
    for (int frame = 1; frame <= k_Length; ++frame)
    {
        SCOPED_TRACE(frame);
        const float offset = NS::DepthShakeOffset(frame, k_Length, 1.2f, k_Push, 0.45f, 7u);
        const int push = (frame - 1) / k_Push;
        const int since = (frame - 1) % k_Push;
        const float envelope = 1.2f * (1.0f - static_cast<float>(push * k_Push) / static_cast<float>(k_Length)) *
                               std::pow(0.45f, static_cast<float>(since));
        EXPECT_GE(offset, envelope * 0.75f - 1.0e-6f);
        EXPECT_LE(offset, envelope + 1.0e-6f);
        // 1 つの押しの間はばらつきが同じなので、戻りの残りの比で減る
        if (since > 0)
        {
            EXPECT_NEAR(offset, NS::DepthShakeOffset(frame - 1, k_Length, 1.2f, k_Push, 0.45f, 7u) * 0.45f, 1.0e-6f);
        }
    }
    EXPECT_FLOAT_EQ(NS::DepthShakeOffset(0, k_Length, 1.2f, k_Push, 0.45f, 7u), 0.0f);
    EXPECT_FLOAT_EQ(NS::DepthShakeOffset(k_Length + 1, k_Length, 1.2f, k_Push, 0.45f, 7u), 0.0f);
    EXPECT_FLOAT_EQ(NS::DepthShakeOffset(3, 0, 1.2f, k_Push, 0.45f, 7u), 0.0f);
    // 押す間が 1 未満なら毎フレーム押し直す
    EXPECT_GT(NS::DepthShakeOffset(2, k_Length, 1.2f, 0, 0.45f, 7u),
              NS::DepthShakeOffset(1, k_Length, 1.2f, 0, 0.45f, 7u) * 0.45f + 1.0e-3f);
    // 違う種は押しのばらつきが違う
    bool differs = false;
    for (int frame = 1; frame <= k_Length; frame += k_Push)
    {
        differs = differs || NS::DepthShakeOffset(frame, k_Length, 1.2f, k_Push, 0.45f, 7u) !=
                                 NS::DepthShakeOffset(frame, k_Length, 1.2f, k_Push, 0.45f, 8u);
    }
    EXPECT_TRUE(differs);
}

// 残像の離れは、横揺れの振れ幅 × 残りから、ばらつきと左右を除いた物。揺れの外では 0
TEST(BodyShake, ReachIsTheEnvelopeWithoutTheSpreadOrSide)
{
    EXPECT_FLOAT_EQ(NS::BodyShakeReach(3, 12, 0.2f), 0.2f * (1.0f - 3.0f / 12.0f));
    EXPECT_FLOAT_EQ(NS::BodyShakeReach(0, 12, 0.2f), 0.0f);
    EXPECT_FLOAT_EQ(NS::BodyShakeReach(12, 12, 0.2f), 0.0f);
    for (int frame = 1; frame < 12; ++frame)
    {
        SCOPED_TRACE(frame);
        EXPECT_LE(std::abs(NS::BodyShakeOffset(frame, 12, 0.2f, 5u, 1.0f, 2)),
                  NS::BodyShakeReach(frame, 12, 0.2f) + 1.0e-6f);
    }
}
