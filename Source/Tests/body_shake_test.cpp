#include "Game/Level/ImpactOutcome.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/Model.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

// 止めの間の横揺れは、体ごと画面の横へ 1 フレームごとに左右を入れ替えて揺らし、止めの終わりで 0 になる
// 揺らすのは描く形だけで、根の位置と当たりは動かさない

namespace
{
    NS::Obj::Actor* PlaceRock(NS::Obj::Scene& scene)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 2);
        NS::Obj::SetObjectPosition(rock, NS::Core::Vector3{2.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.LoadJson(doc);
        return scene.Objects().FindByObjectId(2);
    }
} // namespace

// 振れ幅 × (1 − 経過 ÷ 長さ)² に 0.7〜1 のばらつきを掛け、1 フレーム目は最初の向き、そこから 1 フレームごとに入れ替わる
TEST(BodyShake, OffsetAlternatesAndFallsToZeroByTheEnd)
{
    constexpr int k_Length = 12;
    float previous = 0.0f;
    for (int frame = 1; frame < k_Length; ++frame)
    {
        SCOPED_TRACE(frame);
        const float offset = NS::Game::Level::BodyShakeOffset(frame, k_Length, 0.1f, 7u, -1.0f);
        const float left = 1.0f - static_cast<float>(frame) / static_cast<float>(k_Length);
        const float envelope = 0.1f * left * left;
        EXPECT_GE(std::abs(offset), envelope * 0.7f - 1.0e-6f);
        EXPECT_LE(std::abs(offset), envelope + 1.0e-6f);
        // 1 フレーム目は最初の向き (−)、次は +
        float sign = -1.0f;
        if (frame % 2 == 0)
        {
            sign = 1.0f;
        }
        EXPECT_GT(offset * sign, 0.0f);
        EXPECT_NE(offset, previous);
        previous = offset;
    }
    EXPECT_FLOAT_EQ(NS::Game::Level::BodyShakeOffset(k_Length, k_Length, 0.1f, 7u, -1.0f), 0.0f);
    EXPECT_FLOAT_EQ(NS::Game::Level::BodyShakeOffset(0, k_Length, 0.1f, 7u, -1.0f), 0.0f);
    EXPECT_FLOAT_EQ(NS::Game::Level::BodyShakeOffset(3, 0, 0.1f, 7u, -1.0f), 0.0f);
    // 同じ種は同じ揺れ、違う種はばらつきが違う
    EXPECT_FLOAT_EQ(NS::Game::Level::BodyShakeOffset(2, k_Length, 0.1f, 7u, 1.0f),
                    NS::Game::Level::BodyShakeOffset(2, k_Length, 0.1f, 7u, 1.0f));
    bool differs = false;
    for (int frame = 1; frame < k_Length; ++frame)
    {
        differs = differs || NS::Game::Level::BodyShakeOffset(frame, k_Length, 0.1f, 7u, 1.0f) !=
                                 NS::Game::Level::BodyShakeOffset(frame, k_Length, 0.1f, 8u, 1.0f);
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
    NS::Obj::Model* model = rock->ModelPart();
    ASSERT_NE(model, nullptr);
    const NS::Core::Matrix before = model->DrawWorldMatrix(1.0f);

    ASSERT_TRUE(model->SetDrawOffset(NS::Core::Vector3{0.25f, 0.0f, -0.5f}));
    const NS::Core::Matrix after = model->DrawWorldMatrix(1.0f);
    EXPECT_NEAR(after._41 - before._41, 0.25f, 1.0e-5f);
    EXPECT_NEAR(after._42 - before._42, 0.0f, 1.0e-5f);
    EXPECT_NEAR(after._43 - before._43, -0.5f, 1.0e-5f);
    EXPECT_FLOAT_EQ(rock->Root().Position().x, 2.0f);

    EXPECT_FALSE(model->SetDrawOffset(NS::Core::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}));
    EXPECT_FLOAT_EQ(model->DrawOffset().x, 0.25f);
    ASSERT_TRUE(model->SetDrawOffset(NS::Core::Vector3{0.0f, 0.0f, 0.0f}));
    const NS::Core::Matrix reset = model->DrawWorldMatrix(1.0f);
    EXPECT_FLOAT_EQ(reset._41, before._41);
}
