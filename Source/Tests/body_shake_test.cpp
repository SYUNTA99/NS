#include "Game/Level/ImpactOutcome.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Scene/Scene.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

// 止めの間の横揺れは、体ごと画面の横へ決めたフレーム数ごとに左右を入れ替えて揺らし、止めの終わりで 0 になる
// 揺らすのは描く形だけで、根の位置と当たりは動かさない

namespace
{
    NS::Obj::Actor* PlaceRock(NS::Obj::Scene& scene)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 2);
        NS::Obj::SetObjectPosition(rock, NS::Vector3{2.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.LoadJson(doc);
        return scene.Objects().FindByObjectId(2);
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
        const float offset = NS::Game::Level::BodyShakeOffset(frame, k_Length, 0.1f, 7u, -1.0f, k_Flip);
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
    EXPECT_FLOAT_EQ(NS::Game::Level::BodyShakeOffset(k_Length, k_Length, 0.1f, 7u, -1.0f, k_Flip), 0.0f);
    EXPECT_FLOAT_EQ(NS::Game::Level::BodyShakeOffset(0, k_Length, 0.1f, 7u, -1.0f, k_Flip), 0.0f);
    EXPECT_FLOAT_EQ(NS::Game::Level::BodyShakeOffset(3, 0, 0.1f, 7u, -1.0f, k_Flip), 0.0f);
    // 入れ替えのフレーム数が 1 未満なら 1 フレームごとに入れ替える
    EXPECT_LT(NS::Game::Level::BodyShakeOffset(1, k_Length, 0.1f, 7u, 1.0f, 0) *
                  NS::Game::Level::BodyShakeOffset(2, k_Length, 0.1f, 7u, 1.0f, 0),
              0.0f);
    // 同じ種は同じ揺れ、違う種はばらつきが違う
    EXPECT_FLOAT_EQ(NS::Game::Level::BodyShakeOffset(2, k_Length, 0.1f, 7u, 1.0f, k_Flip),
                    NS::Game::Level::BodyShakeOffset(2, k_Length, 0.1f, 7u, 1.0f, k_Flip));
    bool differs = false;
    for (int frame = 1; frame < k_Length; ++frame)
    {
        differs = differs || NS::Game::Level::BodyShakeOffset(frame, k_Length, 0.1f, 7u, 1.0f, k_Flip) !=
                                 NS::Game::Level::BodyShakeOffset(frame, k_Length, 0.1f, 8u, 1.0f, k_Flip);
    }
    EXPECT_TRUE(differs);
}

// 描く時だけのずれは描く行列の位置にだけ足し、根の位置は変えない。有限でないずれは書かない
// 描く箱へ足す分は、試しの世界にメッシュが無いので見ない
TEST(BodyShake, DrawOffsetMovesOnlyTheDrawnShape)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* rock = PlaceRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::Obj::Model* model = rock->ModelSubObj();
    ASSERT_NE(model, nullptr);
    const NS::Matrix before = model->DrawWorldMatrix(1.0f);

    ASSERT_TRUE(model->SetDrawOffset(NS::Vector3{0.25f, 0.0f, -0.5f}));
    const NS::Matrix after = model->DrawWorldMatrix(1.0f);
    EXPECT_NEAR(after._41 - before._41, 0.25f, 1.0e-5f);
    EXPECT_NEAR(after._42 - before._42, 0.0f, 1.0e-5f);
    EXPECT_NEAR(after._43 - before._43, -0.5f, 1.0e-5f);
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.0f);

    EXPECT_FALSE(model->SetDrawOffset(NS::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}));
    EXPECT_FLOAT_EQ(model->DrawOffset().x, 0.25f);
    ASSERT_TRUE(model->SetDrawOffset(NS::Vector3{0.0f, 0.0f, 0.0f}));
    const NS::Matrix reset = model->DrawWorldMatrix(1.0f);
    EXPECT_FLOAT_EQ(reset._41, before._41);
}

// 残像は根の形を ±離れだけずらした所に描く。描く時だけのずれは残像に足さないので、体が片側へ振れても残像は
// 根を挟んで左右に残る。有限でない離れは書かない
TEST(BodyShake, GhostsSitAtPlusAndMinusTheSpreadAroundTheRoot)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* rock = PlaceRock(scene);
    ASSERT_NE(rock, nullptr);
    NS::Obj::Model* model = rock->ModelSubObj();
    ASSERT_NE(model, nullptr);
    const NS::Matrix root = model->DrawWorldMatrix(1.0f);
    ASSERT_TRUE(model->SetDrawOffset(NS::Vector3{0.1f, 0.0f, 0.0f}));
    ASSERT_TRUE(model->SetGhostSpread(NS::Vector3{0.3f, 0.0f, -0.2f}));
    const NS::Matrix plus = model->GhostWorldMatrix(1.0f, 1.0f);
    const NS::Matrix minus = model->GhostWorldMatrix(1.0f, -1.0f);
    EXPECT_NEAR(plus._41 - root._41, 0.3f, 1.0e-5f);
    EXPECT_NEAR(plus._43 - root._43, -0.2f, 1.0e-5f);
    EXPECT_NEAR(minus._41 - root._41, -0.3f, 1.0e-5f);
    EXPECT_NEAR(minus._43 - root._43, 0.2f, 1.0e-5f);
    EXPECT_FALSE(model->SetGhostSpread(NS::Vector3{std::numeric_limits<float>::infinity(), 0.0f, 0.0f}));
    EXPECT_FLOAT_EQ(model->GhostSpread().x, 0.3f);
}

// 残像の離れは、横揺れの振れ幅の包み (振れ幅 × 残り) に倍率を掛けた物。揺れの外では 0
TEST(BodyShake, ReachIsTheEnvelopeWithoutTheSpreadOrSide)
{
    EXPECT_FLOAT_EQ(NS::Game::Level::BodyShakeReach(3, 12, 0.2f), 0.2f * (1.0f - 3.0f / 12.0f));
    EXPECT_FLOAT_EQ(NS::Game::Level::BodyShakeReach(0, 12, 0.2f), 0.0f);
    EXPECT_FLOAT_EQ(NS::Game::Level::BodyShakeReach(12, 12, 0.2f), 0.0f);
    for (int frame = 1; frame < 12; ++frame)
    {
        SCOPED_TRACE(frame);
        EXPECT_LE(std::abs(NS::Game::Level::BodyShakeOffset(frame, 12, 0.2f, 5u, 1.0f, 2)),
                  NS::Game::Level::BodyShakeReach(frame, 12, 0.2f) + 1.0e-6f);
    }
}
