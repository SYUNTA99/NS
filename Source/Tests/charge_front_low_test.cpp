#include "Game/Player.h"
#include "Game/Player/ChargeEffects.h"
#include "Game/Player/PlayerParams.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Windows/Clock.h"
#include "TestEffectFiles.h"
#include "Tests/TestViewCamera.h"

#pragma warning(push, 0)
#include "Effekseer/Effekseer.EffectNode.h"
#pragma warning(pop)

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// 溜めの光を、カメラから見て手前は低く・奥は高くする。根はカメラの正面の水平の向き (狙いの線) へ毎フレーム回す

namespace
{
    int FramesFor(float seconds)
    {
        return static_cast<int>(std::lround(seconds / NS::OS::FrameTimer::FixedDelta()));
    }

    const NS::Game::Player::EffectLayerRecord* LiveLayer(const NS::Game::Player::ChargeEffects& effects,
                                                         std::string_view name)
    {
        for (const NS::Game::Player::EffectLayerRecord& record : effects.Layers().Records())
        {
            if (record.name == name && !record.endStep.has_value())
            {
                return &record;
            }
        }
        return nullptr;
    }
} // namespace

// 溜まる光の根の向きは、そのフレームの狙いの線の水平の向き。カメラを回すと次のフレームで付いてくる
TEST(ChargeFrontLow, GatherRootFacesTheAimLineEveryFrame)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json entry = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(entry, "Player");
    NS::Obj::SetObjectJsonId(entry, 1);
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
    scene.LoadJson(doc);
    Player* player = static_cast<Player*>(scene.Objects().FindByObjectId(1));
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player->Params(), {{"チャージしきい値秒", 0.2f}}), 0u);
    TestViewCamera* camera = PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
    ASSERT_NE(camera, nullptr);
    for (int frame = 0; frame < FramesFor(0.2f); ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());

    const NS::Vector3 targets[] = {
        NS::Vector3{1.0f, 0.0f, 0.0f}, NS::Vector3{-1.0f, 0.0f, -1.0f}, NS::Vector3{0.0f, 0.0f, 1.0f}};
    for (const NS::Vector3& target : targets)
    {
        camera->SetPose(NS::Vector3{}, target);
        for (int frame = 0; frame < 3; ++frame)
        {
            player->Update(true);
            NS::Game::Level::AimLine aim{};
            ASSERT_TRUE(player->TryGetAimLine(aim));
            const NS::Game::Player::EffectLayerRecord* gather = LiveLayer(player->ChargeVisuals(), "charge.gather");
            ASSERT_NE(gather, nullptr);
            ASSERT_TRUE(gather->rotation.has_value());
            const NS::Quaternion expected = NS::Game::Player::ChargeEffects::YawToward(aim.direction);
            EXPECT_NEAR(gather->rotation->x, expected.x, 0.00001f);
            EXPECT_NEAR(gather->rotation->y, expected.y, 0.00001f);
            EXPECT_NEAR(gather->rotation->z, expected.z, 0.00001f);
            EXPECT_NEAR(gather->rotation->w, expected.w, 0.00001f);
        }
    }
}

// 球の上に生む光の点は、根から見て手前 (−Z、カメラの側) では玉の中心より低い所にだけ生まれ、
// 奥 (+Z) では高い所にも生まれる。点の向きは生む時だけ根に従う
TEST(ChargeFrontLow, PointsNearTheCameraSpawnLowerThanFarOnes)
{
    const Effekseer::EffectRef effect = LoadEffectFile("Assets/Effects/charge.gather.efkefc");
    ASSERT_NE(effect, nullptr);
    std::vector<Effekseer::EffectNode*> nodes;
    CollectEffectNodesInDrawOrder(effect->GetRoot(), nodes);

    int sphereNodes = 0;
    float nearHighest = -std::numeric_limits<float>::infinity();
    float farHighest = -std::numeric_limits<float>::infinity();
    int nearSamples = 0;
    for (Effekseer::EffectNode* node : nodes)
    {
        Effekseer::EffectNodeImplemented* implemented = static_cast<Effekseer::EffectNodeImplemented*>(node);
        const Effekseer::ParameterGenerationLocation& location = implemented->GenerationLocation;
        if (location.type != Effekseer::ParameterGenerationLocation::TYPE_SPHERE)
        {
            continue;
        }
        ++sphereNodes;
        EXPECT_EQ(implemented->CommonValues.RotationBindType, Effekseer::BindType::WhenCreating);
        // 実行側と同じ式で、回転の範囲の格子の上の生む位置を出す (Effekseer.SpawnMethod.h の球)
        constexpr int k_Steps = 24;
        for (int i = 0; i <= k_Steps; ++i)
        {
            const float ax = location.sphere.rotation_x.min +
                             (location.sphere.rotation_x.max - location.sphere.rotation_x.min) * i / k_Steps;
            for (int j = 0; j <= k_Steps; ++j)
            {
                const float ay = location.sphere.rotation_y.min +
                                 (location.sphere.rotation_y.max - location.sphere.rotation_y.min) * j / k_Steps;
                const Effekseer::SIMD::Mat43f spawn = Effekseer::SIMD::Mat43f::Translation(0.0f, 1.0f, 0.0f) *
                                                      Effekseer::SIMD::Mat43f::RotationX(ax) *
                                                      Effekseer::SIMD::Mat43f::RotationY(ay);
                const Effekseer::SIMD::Vec3f position = spawn.GetTranslation();
                if (position.GetZ() < -0.01f)
                {
                    ++nearSamples;
                    nearHighest = std::max(nearHighest, position.GetY());
                }
                else if (position.GetZ() > 0.01f)
                {
                    farHighest = std::max(farHighest, position.GetY());
                }
            }
        }
    }
    EXPECT_GE(sphereNodes, 2);
    ASSERT_GT(nearSamples, 0);
    // 手前は赤道より下だけ、奥は真上まで
    EXPECT_LT(nearHighest, 0.0f);
    EXPECT_GT(farHighest, 0.9f);
}
