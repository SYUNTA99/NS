#include "Game/Level/BodyShake.h"
#include "Game/Level/TackleGhost.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

// 当たりで描く形だけを動かす 3 つの見た目 (横揺れ・描く形の倍率・衝撃の震え) の計算と、Model へ書く走る物を縛る
// 揺らすのは描く形だけで、根の位置と当たりは動かさない

namespace
{
    class ModelActor final : public NS::SceneActor
    {
    public:
        GL::Level::TackleGhost* ghost = nullptr;

    protected:
        void OnInit() override
        {
            CreateClassObj(m_model);
            ghost = CreateChildActor<GL::Level::TackleGhost>();
        }
    };

    GL::Level::BodyShakeDesc SideShakeOf(int length)
    {
        return GL::Level::BodyShakeDesc{.axis = NS::Vector3{1.0f, 0.0f, 0.0f},
                                        .amplitude = 0.2f,
                                        .length = length,
                                        .seed = 7u,
                                        .firstSign = 1.0f,
                                        .flipFrames = 1,
                                        .amplitudeSpread = 0.3f};
    }

    NS::HitReactionSelf SelfOf(ModelActor* actor)
    {
        return NS::HitReactionSelf{.actor = actor, .model = actor->ModelClassObj()};
    }
} // namespace

// 振れ幅 × (1 − 経過 ÷ 長さ) に (1 − ばらつき)〜1 を掛ける。1 フレーム目から入れ替えのフレーム数 (2) の間は最初の向き、
// そこから 2 フレームごとに入れ替わる。止めの後半まで揺れが残るよう、弱まり方は直線
TEST(BodyShake, OffsetHoldsEachSideForTheFlipFramesAndFallsLinearly)
{
    constexpr int k_Length = 12;
    GL::Level::BodyShakeDesc desc{.amplitude = 0.1f,
                                  .length = k_Length,
                                  .seed = 7u,
                                  .firstSign = -1.0f,
                                  .flipFrames = 2,
                                  .amplitudeSpread = 0.3f};
    for (int frame = 1; frame < k_Length; ++frame)
    {
        SCOPED_TRACE(frame);
        const float offset = GL::Level::BodyShakeOffset(desc, frame);
        const float envelope = 0.1f * (1.0f - static_cast<float>(frame) / static_cast<float>(k_Length));
        EXPECT_GE(std::abs(offset), envelope * 0.7f - 1.0e-6f);
        EXPECT_LE(std::abs(offset), envelope + 1.0e-6f);
        // 1・2 フレーム目は最初の向き (−)、3・4 は +、5・6 は −
        float sign = -1.0f;
        if (((frame - 1) / desc.flipFrames) % 2 == 1)
        {
            sign = 1.0f;
        }
        EXPECT_GT(offset * sign, 0.0f);
    }
    EXPECT_FLOAT_EQ(GL::Level::BodyShakeOffset(desc, k_Length), 0.0f);
    EXPECT_FLOAT_EQ(GL::Level::BodyShakeOffset(desc, 0), 0.0f);
    GL::Level::BodyShakeDesc empty = desc;
    empty.length = 0;
    EXPECT_FLOAT_EQ(GL::Level::BodyShakeOffset(empty, 3), 0.0f);
    // ばらつき 0 なら振れ幅は包みそのもの
    GL::Level::BodyShakeDesc even = desc;
    even.amplitudeSpread = 0.0f;
    EXPECT_FLOAT_EQ(std::abs(GL::Level::BodyShakeOffset(even, 3)), GL::Level::BodyShakeReach(3, k_Length, 0.1f));
    // ばらつきは 0〜1 に収める。外の値でも包みを超えず、最初の向きも変わらない
    for (const float outside : {1.5f, -0.5f})
    {
        SCOPED_TRACE(outside);
        GL::Level::BodyShakeDesc wild = desc;
        wild.amplitudeSpread = outside;
        for (int frame = 1; frame <= desc.flipFrames; ++frame)
        {
            EXPECT_LT(GL::Level::BodyShakeOffset(wild, frame), 0.0f);
            EXPECT_LE(std::abs(GL::Level::BodyShakeOffset(wild, frame)),
                      GL::Level::BodyShakeReach(frame, k_Length, 0.1f) + 1.0e-6f);
        }
    }
    // 入れ替えのフレーム数が 1 未満なら 1 フレームごとに入れ替える
    GL::Level::BodyShakeDesc every = desc;
    every.flipFrames = 0;
    EXPECT_LT(GL::Level::BodyShakeOffset(every, 1) * GL::Level::BodyShakeOffset(every, 2), 0.0f);
    // 違う種はばらつきが違う
    GL::Level::BodyShakeDesc other = desc;
    other.seed = 8u;
    bool differs = false;
    for (int frame = 1; frame < k_Length; ++frame)
    {
        differs = differs || GL::Level::BodyShakeOffset(desc, frame) != GL::Level::BodyShakeOffset(other, frame);
    }
    EXPECT_TRUE(differs);
}

// 横揺れの走る物は、最初の Advance を 1 フレーム目に数えてずれを描く行列の位置にだけ足し、根の位置は変えない。
// 長さのフレームで 0 になり、その次の Advance で 0 を書いて終わる。止めると元の位置で描く
TEST(BodyShake, ShakeRunMovesOnlyTheDrawnShapeAndEndsAfterTheLength)
{
    NS::Scene scene;
    ModelActor* actor = scene.SpawnActor<ModelActor>("ModelActor");
    actor->Root().SetPosition(NS::Vector3{2.0f, 1.0f, 0.0f});
    NS::Model* model = actor->ModelClassObj();
    ASSERT_NE(model, nullptr);
    const NS::Matrix before = model->DrawWorldMatrix(actor->Root().WorldMatrix(), 1.0f);

    constexpr int k_Length = 4;
    const GL::Level::BodyShakeDesc shake = SideShakeOf(k_Length);
    GL::Level::BodyShakeRun run(shake);
    for (int frame = 1; frame <= k_Length; ++frame)
    {
        SCOPED_TRACE(frame);
        EXPECT_TRUE(run.Advance(SelfOf(actor)));
        EXPECT_FLOAT_EQ(model->GetDrawOffset().x, shake.OffsetAt(frame).x);
    }
    EXPECT_FALSE(run.Advance(SelfOf(actor)));
    EXPECT_FLOAT_EQ(model->GetDrawOffset().x, 0.0f);

    GL::Level::BodyShakeRun moved(shake);
    ASSERT_TRUE(moved.Advance(SelfOf(actor)));
    const NS::Vector3 offset = shake.OffsetAt(1);
    ASSERT_NE(offset.x, 0.0f);
    const NS::Matrix after = model->DrawWorldMatrix(actor->Root().WorldMatrix(), 1.0f);
    EXPECT_NEAR(after._41 - before._41, offset.x, 1.0e-5f);
    EXPECT_NEAR(after._42 - before._42, 0.0f, 1.0e-5f);
    EXPECT_FLOAT_EQ(actor->Root().Position().x, 2.0f);

    moved.Stop(SelfOf(actor));
    const NS::Matrix reset = model->DrawWorldMatrix(actor->Root().WorldMatrix(), 1.0f);
    EXPECT_FLOAT_EQ(reset._41, before._41);
}

// 弱めると、次の Advance から振れ幅に倍率が掛かる
TEST(BodyShake, ScalingTheShakeRunScalesTheNextOffset)
{
    NS::Scene scene;
    ModelActor* actor = scene.SpawnActor<ModelActor>("ModelActor");
    const GL::Level::BodyShakeDesc shake = SideShakeOf(6);
    GL::Level::BodyShakeRun run(shake);
    run.Scale(0.5f);
    ASSERT_TRUE(run.Advance(SelfOf(actor)));
    EXPECT_NEAR(actor->ModelClassObj()->GetDrawOffset().x, shake.OffsetAt(1).x * 0.5f, 1.0e-6f);
}

// 何も書いていない Model は、ずれ 0・倍率 1・震えなしで描く
TEST(BodyShake, ModelWithoutRequestsDrawsUnadjusted)
{
    NS::Scene scene;
    ModelActor* actor = scene.SpawnActor<ModelActor>("ModelActor");
    NS::Model* model = actor->ModelClassObj();
    ASSERT_NE(model, nullptr);
    EXPECT_TRUE(model->GetDrawOffset() == (NS::Vector3{0.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(model->GetDrawScale() == (NS::Vector3{1.0f, 1.0f, 1.0f}));
    EXPECT_FLOAT_EQ(GL::Level::BodyTremor::FromDrawValues(model->DrawValues()).amplitude, 0.0f);
}

// 形の走る物は、最初の Advance を 0 フレーム目に数えて曲線の倍率を書き、長さのフレームで偽を返す。止めると倍率 1 へ戻す
TEST(BodyShake, ShapeRunFollowsTheCurveAndEndsAtTheLength)
{
    NS::Scene scene;
    ModelActor* actor = scene.SpawnActor<ModelActor>("ModelActor");
    NS::Model* model = actor->ModelClassObj();
    ASSERT_NE(model, nullptr);
    constexpr int k_Length = 3;
    GL::Level::BodyShapeDesc shape{.length = k_Length};
    shape.height.count = 2;
    shape.height.keys[0] = NS::Curve::Key{0.0f, 2.0f};
    shape.height.keys[1] = NS::Curve::Key{2.0f, 1.5f};
    GL::Level::BodyShapeRun run(shape);
    for (int frame = 0; frame < k_Length; ++frame)
    {
        SCOPED_TRACE(frame);
        EXPECT_TRUE(run.Advance(SelfOf(actor)));
        EXPECT_FLOAT_EQ(model->GetDrawScale().y, shape.ScaleAt(frame).y);
    }
    EXPECT_FALSE(run.Advance(SelfOf(actor)));
    run.Stop(SelfOf(actor));
    EXPECT_TRUE(model->GetDrawScale() == (NS::Vector3{1.0f, 1.0f, 1.0f}));
}

// 震えの走る物は、最初の Advance を 0 フレーム目に数え、長さのフレームで振れ幅 0 を書いて偽を返す。止めると振れ幅 0
TEST(BodyShake, TremorRunStartsAtFrameZeroAndEndsAtTheLength)
{
    NS::Scene scene;
    ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{0.0f, 0.0f, -5.0f}, NS::Vector3{}), nullptr);
    ModelActor* actor = scene.SpawnActor<ModelActor>("ModelActor");
    NS::Model* model = actor->ModelClassObj();
    ASSERT_NE(model, nullptr);
    constexpr int k_Length = 3;
    const GL::Level::BodyTremorDesc desc{.contactOffset = NS::Vector3{0.0f, 0.5f, 0.0f},
                                         .amplitudePixels = 4.0f,
                                         .length = k_Length,
                                         .referenceHeight = 720.0f,
                                         .bodyLength = 1.0f};
    GL::Level::BodyTremorRun run(desc);
    for (int frame = 0; frame < k_Length; ++frame)
    {
        SCOPED_TRACE(frame);
        EXPECT_TRUE(run.Advance(SelfOf(actor)));
        const GL::Level::BodyTremor tremor = GL::Level::BodyTremor::FromDrawValues(model->DrawValues());
        EXPECT_GT(tremor.amplitude, 0.0f);
        EXPECT_FLOAT_EQ(tremor.elapsedFrames, static_cast<float>(frame));
        EXPECT_FLOAT_EQ(tremor.contactOffset.y, 0.5f);
    }
    EXPECT_FALSE(run.Advance(SelfOf(actor)));
    EXPECT_FLOAT_EQ(GL::Level::BodyTremor::FromDrawValues(model->DrawValues()).amplitude, 0.0f);

    GL::Level::BodyTremorRun stopped(desc);
    ASSERT_TRUE(stopped.Advance(SelfOf(actor)));
    const float full = GL::Level::BodyTremor::FromDrawValues(model->DrawValues()).amplitude;
    ASSERT_GT(full, 0.0f);
    // 弱めると次の Advance から振れ幅に倍率が掛かる。ピクセルから直す距離は変わらない
    stopped.Scale(0.5f);
    ASSERT_TRUE(stopped.Advance(SelfOf(actor)));
    EXPECT_NEAR(GL::Level::BodyTremor::FromDrawValues(model->DrawValues()).amplitude, full * 0.5f, 1.0e-6f);
    stopped.Stop(SelfOf(actor));
    EXPECT_FLOAT_EQ(GL::Level::BodyTremor::FromDrawValues(model->DrawValues()).amplitude, 0.0f);
}

// 震えの並びは DrawValues と FromDrawValues で行き来しても欄が変わらない。シェーダの側の並びは TremorShader.* が見る
TEST(BodyShake, TremorDrawValuesRoundTrip)
{
    const GL::Level::BodyTremor tremor{.contactOffset = NS::Vector3{1.0f, 2.0f, 3.0f},
                                       .amplitude = 4.0f,
                                       .right = NS::Vector3{5.0f, 6.0f, 7.0f},
                                       .elapsedFrames = 8.0f,
                                       .up = NS::Vector3{9.0f, 10.0f, 11.0f},
                                       .framesPerMeter = 12.0f,
                                       .ringFrames = 13.0f};
    const std::array<NS::Vector4, 4> values = tremor.DrawValues();
    EXPECT_FLOAT_EQ(values[0].w, 4.0f);
    EXPECT_FLOAT_EQ(values[3].x, 13.0f);
    const GL::Level::BodyTremor back = GL::Level::BodyTremor::FromDrawValues(values);
    EXPECT_TRUE(back.contactOffset == tremor.contactOffset);
    EXPECT_FLOAT_EQ(back.amplitude, 4.0f);
    EXPECT_TRUE(back.right == tremor.right);
    EXPECT_FLOAT_EQ(back.elapsedFrames, 8.0f);
    EXPECT_TRUE(back.up == tremor.up);
    EXPECT_FLOAT_EQ(back.framesPerMeter, 12.0f);
    EXPECT_FLOAT_EQ(back.ringFrames, 13.0f);
}

// 描く倍率は固定フレームの頭の値から補間し、Snap は補間しない。有限の正でない倍率は断って前の値を残す
TEST(BodyShake, DrawScaleInterpolatesAndSnapDoesNot)
{
    NS::Scene scene;
    ModelActor* actor = scene.SpawnActor<ModelActor>("ModelActor");
    NS::Model* model = actor->ModelClassObj();
    ASSERT_NE(model, nullptr);
    model->Snapshot();
    ASSERT_TRUE(model->SetDrawScale(NS::Vector3{1.0f, 0.5f, 1.0f}));
    EXPECT_NEAR(model->DrawWorldMatrix(actor->Root().InterpolatedWorldMatrix(0.0f), 0.0f)._22, 1.0f, 1.0e-5f);
    EXPECT_NEAR(model->DrawWorldMatrix(actor->Root().WorldMatrix(), 1.0f)._22, 0.5f, 1.0e-5f);

    ASSERT_TRUE(model->SnapDrawScale(NS::Vector3{1.0f, 2.0f, 1.0f}));
    EXPECT_NEAR(model->DrawWorldMatrix(actor->Root().InterpolatedWorldMatrix(0.0f), 0.0f)._22, 2.0f, 1.0e-5f);

    EXPECT_FALSE(model->SetDrawScale(NS::Vector3{1.0f, 0.0f, 1.0f}));
    EXPECT_FALSE(model->SnapDrawScale(NS::Vector3{1.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f}));
    EXPECT_FLOAT_EQ(model->GetDrawScale().y, 2.0f);
}

// 残像は根の形を ±離れだけずらした所に描く。描く時だけのずれは残像に足さないので、体が片側へ振れても残像は
// 根を挟んで左右に残る。有限でない離れは書かない
TEST(BodyShake, GhostsSitAtPlusAndMinusTheSpreadAroundTheRoot)
{
    NS::Scene scene;
    ModelActor* actor = scene.SpawnActor<ModelActor>("ModelActor");
    actor->Root().SetPosition(NS::Vector3{2.0f, 1.0f, 0.0f});
    NS::Model* model = actor->ModelClassObj();
    ASSERT_NE(model, nullptr);
    const NS::Matrix root = model->DrawWorldMatrix(actor->Root().WorldMatrix(), 1.0f);
    GL::Level::BodyShakeRun run(SideShakeOf(6));
    ASSERT_TRUE(run.Advance(SelfOf(actor)));
    ASSERT_NE(model->GetDrawOffset().x, 0.0f);
    ASSERT_TRUE(actor->ghost->SetSpread(NS::Vector3{0.3f, 0.0f, -0.2f}));
    const NS::Matrix plus = actor->ghost->SideWorldMatrix(model, actor->Root().WorldMatrix(), 1.0f, 1.0f);
    const NS::Matrix minus = actor->ghost->SideWorldMatrix(model, actor->Root().WorldMatrix(), 1.0f, -1.0f);
    EXPECT_NEAR(plus._41 - root._41, 0.3f, 1.0e-5f);
    EXPECT_NEAR(plus._43 - root._43, -0.2f, 1.0e-5f);
    EXPECT_NEAR(minus._41 - root._41, -0.3f, 1.0e-5f);
    EXPECT_NEAR(minus._43 - root._43, 0.2f, 1.0e-5f);
    EXPECT_FALSE(actor->ghost->SetSpread(NS::Vector3{std::numeric_limits<float>::infinity(), 0.0f, 0.0f}));
    EXPECT_FLOAT_EQ(actor->ghost->Spread().x, 0.3f);
}

// 残像の離れは、横揺れの振れ幅 × 残りから、ばらつきと左右を除いた物。揺れの外では 0
TEST(BodyShake, ReachIsTheEnvelopeWithoutTheSpreadOrSide)
{
    EXPECT_FLOAT_EQ(GL::Level::BodyShakeReach(3, 12, 0.2f), 0.2f * (1.0f - 3.0f / 12.0f));
    EXPECT_FLOAT_EQ(GL::Level::BodyShakeReach(0, 12, 0.2f), 0.0f);
    EXPECT_FLOAT_EQ(GL::Level::BodyShakeReach(12, 12, 0.2f), 0.0f);
    GL::Level::BodyShakeDesc desc = SideShakeOf(12);
    desc.seed = 5u;
    desc.flipFrames = 2;
    for (int frame = 1; frame < 12; ++frame)
    {
        SCOPED_TRACE(frame);
        EXPECT_LE(std::abs(GL::Level::BodyShakeOffset(desc, frame)),
                  GL::Level::BodyShakeReach(frame, 12, desc.amplitude) + 1.0e-6f);
    }
}

// カメラの向きが非数なら、ピクセルを m に直した値は 0。非数を揺れの大きさへ渡さない
TEST(BodyShake, PixelsToMetersIsZeroForANonFiniteCameraDirection)
{
    NS::CameraPose pose{};
    pose.target.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(NS::ScreenPixelsToMeters(10.0f, pose, NS::Vector3{0.0f, 0.0f, 0.0f}), 0.0f);
}
