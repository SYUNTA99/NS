#include "Game/Player/ChargeEffects.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/EffectKeeper.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Windows/Clock.h"
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

    const NS::EffectRecord* LiveEffect(const PlayerActor* player, std::string_view name)
    {
        NS_ASSERT(Game, player != nullptr, "LiveEffect: 渡したアクターか部品が無い");
        for (const NS::EffectRecord& record : player->GetEffectKeeper()->Records())
        {
            if (record.name == name && !record.endStep.has_value())
            {
                return &record;
            }
        }
        return nullptr;
    }
} // namespace

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
